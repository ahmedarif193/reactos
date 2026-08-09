#!/usr/bin/env python3
"""
VM Monitor Script
Builds the selected boot image, starts VM (VirtualBox or QEMU) and monitors for stalls.
If log stops updating for more than 15 seconds, or total runtime exceeds 60 seconds,
forcefully stops the VM.

When the kernel is booted with /SMPDIAG, the monitor turns each one-second
SMPSTAT block into a live per-CPU table and prints a normalized run summary on
exit. Set ROS_VM_SMP_TABLE=0 to suppress these tables.

Usage:
	  # Run from within your build directory (e.g., output-arm64)
	  python3 ../vm_monitor.py

	  # QEMU path: build livecd and boot livecd.iso
	  python3 ../vm_monitor.py --qemu

	  # QEMU disk-image path: build reactosimg and boot ReactOS.img
	  python3 ../vm_monitor.py --qemu --img

	  # Build and boot LiveCD ISO
	  python3 ../vm_monitor.py --livecd

	  # Boot an existing ISO
	  python3 ../vm_monitor.py --iso path/to/livecd.iso

	  # Boot AMD64 LiveCD, launch the 32-bit winver.exe, and capture the screen
	  python3 ../scripts/vm_monitor.py --run-winver
  
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
import re
import bisect
import glob
import socket

# Configuration (never change those values)
LOG_FILE = "/tmp/freeldr_arm64.log"
STALL_TIMEOUT = int(os.environ.get("ROS_VM_STALL_TIMEOUT", "15"))
HARD_TIMEOUT = int(os.environ.get("ROS_VM_HARD_TIMEOUT", "60"))
VM_NAME = os.environ.get("ROS_VM_NAME", "ROS11")
ENABLE_GDB_DUMP = os.environ.get("ROS_VM_GDB_DUMP", "1") != "0"
QEMU_GDB_PORT = int(os.environ.get("ROS_QEMU_GDB_PORT", "1234"))
QEMU_ARM64_GIC_VERSION = os.environ.get("ROS_QEMU_GIC_VERSION", "auto")
QEMU_ARM64_MEMORY = os.environ.get("ROS_QEMU_ARM64_MEMORY", "24G")
ENABLE_SMP_TABLE = os.environ.get("ROS_VM_SMP_TABLE", "1") != "0"
KERNEL_TEXT_ADDRESS = (
    int(os.environ["ROS_KERNEL_TEXT"], 0)
    if "ROS_KERNEL_TEXT" in os.environ else None
)
CRASH_LOG_MARKERS = (
    "*** Fatal System Error:",
    "*** Assertion failed:",
    "Entered debugger on embedded breakpoint",
    "Entered debugger on last-chance exception",
    "Break repeatedly, break Once",
)
SUCCESS_LOG_MARKER = "BOOT_TESTS_DONE"
KERNEL_LOG_MARKERS = (
    "Command Line: HAL=",
    "SMPSTAT reporter started",
)

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
REACTOS_IMG = os.path.realpath(os.path.join(BUILD_DIR, "ReactOS.img"))
LIVECD_ISO = os.path.realpath(os.path.join(BUILD_DIR, "livecd.iso"))
POST_BOOT_PROGRAM = None
POST_BOOT_DELAY = float(os.environ.get("ROS_VM_POST_BOOT_DELAY", "8"))
POST_BOOT_CAPTURE_DELAY = float(os.environ.get("ROS_VM_POST_BOOT_CAPTURE_DELAY", "5"))
POST_BOOT_SCREENSHOT = os.path.join(BUILD_DIR, "vm_monitor_post_boot.ppm")
QEMU_MONITOR_SOCKET = f"/tmp/reactos_vm_monitor_{os.getpid()}.sock"

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
boot_media = "disk"
boot_image_path = REACTOS_IMG
vm_cleanup_done = False


def read_cmake_cache_arch():
    """Return ARCH from CMakeCache.txt when the build tree has it."""
    cache_path = os.path.join(BUILD_DIR, "CMakeCache.txt")

    try:
        with open(cache_path, "r", encoding="utf-8", errors="replace") as cache_file:
            for line in cache_file:
                if line.startswith("ARCH:STRING="):
                    arch = line.split("=", 1)[1].strip().lower()
                    if arch in ("i386", "amd64", "arm64"):
                        return arch
    except OSError:
        pass

    return None


def detect_target_arch():
    """Detect target architecture from the current build tree."""
    global target_arch

    cache_arch = read_cmake_cache_arch()
    if cache_arch:
        target_arch = cache_arch
        return target_arch

    build_dir_lower = os.path.basename(BUILD_DIR).lower()
    
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
    """Create a FAT32 disk image for USB storage testing."""
    if os.path.exists(FAT32_IMG):
        print(f"FAT32 image already exists: {FAT32_IMG}")
        return True

    print(f"Creating FAT32 image: {FAT32_IMG}")
    try:
        subprocess.run(["dd", "if=/dev/zero", f"of={FAT32_IMG}", "bs=1M", "count=64"],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        subprocess.run(["mkfs.vfat", "-F", "32", FAT32_IMG],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return True
    except Exception as e:
        print(f"Error creating FAT32 image: {e}")
        return False


def resolve_process_path(path, process_cwd):
    """Resolve a path as the target process would have resolved it."""
    if os.path.isabs(path):
        return os.path.realpath(path)
    if process_cwd:
        return os.path.realpath(os.path.join(process_cwd, path))
    return os.path.realpath(path)


def qemu_cmdline_uses_image(cmdline, image_path, process_cwd=None):
    """Return True if a QEMU command line references the exact image path."""
    if not cmdline:
        return False

    exe_name = os.path.basename(cmdline[0])
    if not exe_name.startswith("qemu-system-"):
        return False

    image_real = os.path.realpath(image_path)
    args = cmdline[1:]
    for index, arg in enumerate(args):
        if arg == image_path or resolve_process_path(arg, process_cwd) == image_real:
            return True

        if arg in ("-cdrom", "-hda", "-drive") and index + 1 < len(args):
            candidate_arg = args[index + 1]
            if arg in ("-cdrom", "-hda"):
                if resolve_process_path(candidate_arg, process_cwd) == image_real:
                    return True
            else:
                for item in candidate_arg.split(","):
                    if item.startswith("file=") or item.startswith("file.filename="):
                        candidate = item.split("=", 1)[1]
                        if resolve_process_path(candidate, process_cwd) == image_real:
                            return True

        for item in arg.split(","):
            if item.startswith("file=") or item.startswith("file.filename="):
                candidate = item.split("=", 1)[1]
                if resolve_process_path(candidate, process_cwd) == image_real:
                    return True

            if item.startswith("file:"):
                candidate = item.split(":", 1)[1]
                if resolve_process_path(candidate, process_cwd) == image_real:
                    return True

        if arg.startswith("-drive") and f"file={image_path}" in arg:
            for item in arg.split(","):
                if item.startswith("file=") and resolve_process_path(item.split("=", 1)[1], process_cwd) == image_real:
                    return True

        if arg.startswith("-hda=") or arg.startswith("-cdrom="):
            candidate = arg.split("=", 1)[1]
            if resolve_process_path(candidate, process_cwd) == image_real:
                return True

    return False


def read_process_cmdline(pid):
    """Read a process command line from /proc, returning [] if unavailable."""
    try:
        with open(f"/proc/{pid}/cmdline", "rb") as f:
            raw = f.read()
    except OSError:
        return []

    return [part.decode(errors="replace") for part in raw.split(b"\0") if part]


def read_process_cwd(pid):
    """Read a process cwd from /proc, returning None if unavailable."""
    try:
        return os.readlink(f"/proc/{pid}/cwd")
    except OSError:
        return None


def find_qemu_pids_for_image(image_path):
    """Find QEMU processes that use the specified OS image."""
    pids = []
    proc_root = "/proc"
    if not os.path.isdir(proc_root):
        return pids

    current_pid = os.getpid()
    for entry in os.listdir(proc_root):
        if not entry.isdigit():
            continue

        pid = int(entry)
        if pid == current_pid:
            continue

        cmdline = read_process_cmdline(pid)
        process_cwd = read_process_cwd(pid)
        if qemu_cmdline_uses_image(cmdline, image_path, process_cwd):
            pids.append((pid, cmdline))

    return pids


def process_is_running(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def kill_qemu_for_image(image_path, reason, quiet_not_found=False):
    """Kill only QEMU processes that are using the ReactOS image for this build."""
    matches = find_qemu_pids_for_image(image_path)
    if not matches:
        if not quiet_not_found:
            print(f"No existing QEMU process found for {image_path}.")
        return

    print(f"Stopping {len(matches)} QEMU process(es) for {image_path} ({reason})...")
    for pid, cmdline in matches:
        print(f"  PID {pid}: {' '.join(cmdline)}")
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError as e:
            print(f"  Warning: failed to terminate PID {pid}: {e}")

    deadline = time.time() + 5
    while time.time() < deadline:
        if not any(process_is_running(pid) for pid, _ in matches):
            return
        time.sleep(0.1)

    for pid, _ in matches:
        if process_is_running(pid):
            try:
                print(f"  PID {pid} did not exit; sending SIGKILL")
                os.kill(pid, signal.SIGKILL)
            except OSError as e:
                print(f"  Warning: failed to kill PID {pid}: {e}")


def port_is_available(port):
    """Return True if a local TCP port is currently free."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False


def add_qemu_gdb_server(qemu_cmd):
    """Expose QEMU's GDB stub when the monitor is configured to dump state."""
    if not ENABLE_GDB_DUMP:
        return

    if not port_is_available(QEMU_GDB_PORT):
        print(f"Warning: GDB port {QEMU_GDB_PORT} is busy; VM state dumps disabled.")
        return

    qemu_cmd.extend(["-gdb", f"tcp::{QEMU_GDB_PORT}"])


def remove_file_if_exists(path):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass


def add_qemu_control_monitor(qemu_cmd):
    """Add a private HMP socket for post-boot keyboard and screenshot control."""
    if not POST_BOOT_PROGRAM:
        return

    remove_file_if_exists(QEMU_MONITOR_SOCKET)
    qemu_cmd.extend([
        "-monitor",
        f"unix:{QEMU_MONITOR_SOCKET},server=on,wait=off",
    ])


def connect_qemu_control_monitor(timeout=5):
    deadline = time.time() + timeout
    last_error = None

    while time.time() < deadline:
        monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            monitor.connect(QEMU_MONITOR_SOCKET)
            monitor.settimeout(0.2)
            try:
                monitor.recv(4096)
            except socket.timeout:
                pass
            return monitor
        except OSError as error:
            last_error = error
            monitor.close()
            time.sleep(0.1)

    raise RuntimeError(f"could not connect to QEMU monitor: {last_error}")


def send_hmp_command(monitor, command, delay=0.05):
    monitor.sendall((command + "\n").encode("ascii"))
    time.sleep(delay)


def qemu_key_for_character(character):
    special_keys = {
        ":": "shift-semicolon",
        "\\": "backslash",
        "/": "slash",
        ".": "dot",
        "-": "minus",
        "_": "shift-minus",
        " ": "spc",
    }

    if character in special_keys:
        return special_keys[character]
    if character.isascii() and character.isalnum():
        return character.lower()
    raise ValueError(f"no QEMU key mapping for {character!r}")


def launch_post_boot_program():
    """Open the ReactOS Run dialog and type the configured program path."""
    with connect_qemu_control_monitor() as monitor:
        send_hmp_command(monitor, "sendkey meta_l-r 100", delay=1)
        for character in POST_BOOT_PROGRAM:
            key = qemu_key_for_character(character)
            send_hmp_command(monitor, f"sendkey {key} 20")
        send_hmp_command(monitor, "sendkey ret 100", delay=0.5)


def capture_post_boot_screenshot():
    """Capture the primary QEMU display through HMP."""
    remove_file_if_exists(POST_BOOT_SCREENSHOT)
    with connect_qemu_control_monitor() as monitor:
        send_hmp_command(monitor, f"screendump {POST_BOOT_SCREENSHOT}", delay=1)
    return os.path.isfile(POST_BOOT_SCREENSHOT) and os.path.getsize(POST_BOOT_SCREENSHOT) > 0


def force_kill_vm():
    """Forcefully kill VM - called on exit."""
    global qemu_process, use_qemu, boot_image_path, vm_cleanup_done

    if vm_cleanup_done:
        return
    vm_cleanup_done = True

    if use_qemu:
        if qemu_process:
            try:
                qemu_process.terminate()
                qemu_process.wait(timeout=5)
            except Exception:
                try:
                    qemu_process.kill()
                except Exception:
                    pass
            qemu_process = None
        kill_qemu_for_image(boot_image_path, "monitor cleanup", quiet_not_found=True)
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

    remove_file_if_exists(QEMU_MONITOR_SOCKET)


def build_ninja_target(target):
    """Build a Ninja target before starting VM."""
    print(f"Building {target} in {BUILD_DIR}...")
    try:
        build_env = os.environ.copy()
        cached_clang = "/Users/mac/.local/opt/rosbe/llvm-mingw/bin"
        if os.path.isdir(cached_clang):
            build_env["PATH"] = cached_clang + os.pathsep + build_env.get("PATH", "")

        result = subprocess.run(
            ["ninja", target],
            cwd=BUILD_DIR,
            env=build_env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=600
        )
        if result.returncode != 0:
            print(f"Build failed with return code {result.returncode}")
            if result.stdout:
                print(result.stdout.decode(errors="replace"))
            return False
        print("Build completed successfully.")
        return True
    except Exception as e:
        print(f"Error building {target}: {e}")
        return False


def build_reactosimg():
    """Build reactosimg using ninja before starting VM."""
    return build_ninja_target("reactosimg")


def qemu_iso_drive_args(image_path):
    """Return generic QEMU args for booting an ISO as optical media."""
    return [
        "-drive", f"file={image_path},media=cdrom,readonly=on",
        "-boot", "order=d,menu=on",
    ]


def qemu_arm64_iso_drive_args(image_path):
    """Return ARM64 virt optical media args matching the tested Win/ReactOS path."""
    return [
        "-drive", f"if=virtio,media=cdrom,readonly=on,file={image_path}",
        "-boot", "order=d,menu=on",
    ]


def qemu_arm64_disk_drive_args(image_path):
    """Return ARM64 disk args for firmware boot and kernel storage discovery."""
    return [
        "-drive", f"file.driver=file,file.filename={image_path},file.locking=off,format=raw",
        "-device", "ich9-ahci,id=ahci",
        "-drive", f"if=none,id=ahcidisk,format=raw,file.driver=file,file.filename={image_path},file.locking=off",
        "-device", "ide-hd,drive=ahcidisk,bus=ahci.0",
        "-boot", "order=d,menu=on",
    ]


def resolve_qemu_arm64_gic_version(rpi_mode, is_darwin=False):
    """Return the QEMU virt GIC version to use for ARM64."""
    requested = str(QEMU_ARM64_GIC_VERSION).strip().lower()
    valid_versions = {"auto", "2", "3", "4", "host", "max"}

    if requested not in valid_versions:
        print(f"Invalid ARM64 GIC version '{QEMU_ARM64_GIC_VERSION}', using auto.")
        requested = "auto"

    if requested != "auto":
        return requested

    if rpi_mode:
        return "2"

    return None if is_darwin else "max"


def qemu_arm64_machine_arg(rpi_mode, is_darwin=False):
    """Return the QEMU virt machine argument for ARM64."""
    machine_parts = ["virt"]
    gic_version = resolve_qemu_arm64_gic_version(rpi_mode, is_darwin)

    if is_darwin and not rpi_mode:
        machine_parts.append("accel=hvf")

    if gic_version:
        machine_parts.append(f"gic-version={gic_version}")

    return ",".join(machine_parts), gic_version


def qemu_ahci_iso_args(image_path):
    """Return QEMU q35/AHCI optical media args."""
    return [
        "-device", "ich9-ahci,id=ahci",
        "-drive", f"if=none,id=cdrom0,media=cdrom,readonly=on,file={image_path}",
        "-device", "ide-cd,drive=cdrom0,bus=ahci.0",
        "-boot", "order=d,menu=on",
    ]


def start_qemu(rpi_mode=False, smp=4):
    """Start QEMU based on architecture."""
    global qemu_process, target_arch, boot_media, boot_image_path

    img_path = boot_image_path
    is_iso_boot = (boot_media == "iso")

    smp_arg = os.environ.get("ROS_QEMU_SMP", str(smp))
    numa_args = os.environ.get("ROS_QEMU_NUMA", "").split()

    # Reset log file
    try:
        open(LOG_FILE, 'w').close()
    except Exception as e:
        print(f"Error resetting log file: {e}")
        return False

    # ---------------- ARM64 CONFIGURATION ----------------
    if target_arch == "arm64":
        is_darwin = platform.system() == "Darwin"
        machine_arg, gic_version = qemu_arm64_machine_arg(rpi_mode, is_darwin)
        arm64_usb_devices = [
            "-device", "qemu-xhci,id=xhci",
            "-device", "usb-kbd,bus=xhci.0",
            "-device", "usb-tablet,bus=xhci.0",
        ]
        gic_desc = f", GIC {gic_version}" if gic_version else ""
        if rpi_mode:
            mode_str = f"RPI emulation (cortex-a72, {smp} cores{gic_desc})"
        else:
            mode_str = (
                f"HVF accelerated (max, {smp} cores{gic_desc})"
                if is_darwin else
                f"CPU max ({smp} cores{gic_desc})"
            )
        print(f"Starting QEMU (ARM64 - {mode_str}, {QEMU_ARM64_MEMORY} RAM)...")

        # Darwin-specific configuration (macOS)
        if is_darwin:
            if rpi_mode:
                # Raspberry Pi emulation mode (cortex-a72, no HVF)
                # Use -serial stdio with stdout redirect instead of -serial file:
                # because -serial file: has buffering issues on macOS QEMU
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", smp_arg, *numa_args,
                    "-device", "ramfb",
                    *arm64_usb_devices,
                    "-machine", machine_arg,
                    "-cpu", "cortex-a72",
                    "-m", QEMU_ARM64_MEMORY,
                    "-drive", "if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-aarch64-code.fd",
                    *(qemu_arm64_iso_drive_args(img_path) if is_iso_boot else qemu_arm64_disk_drive_args(img_path)),
                    "-display", "none",
                    "-serial", "stdio"
                ]
            else:
                # HVF accelerated mode (max CPU features)
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-device", "ramfb",
                    *arm64_usb_devices,
                    "-machine", machine_arg,
                    "-cpu", "max",
                    "-smp", smp_arg, *numa_args,
                    "-m", QEMU_ARM64_MEMORY,
                    "-drive", "if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-aarch64-code.fd",
                    *(qemu_arm64_iso_drive_args(img_path) if is_iso_boot else qemu_arm64_disk_drive_args(img_path)),
                    "-display", "none",
                    "-serial", f"file:{LOG_FILE}"
                ]
        else:
            # Linux/other systems
            # Dual-attach strategy:
            #   1. VirtIO drive -> UEFI can enumerate and boot from it.
            #   2. AHCI + IDE disk -> ReactOS has a boot-start storage path that
            #      can create \Device\Harddisk0\Partition1 for the ARC name.
            # Without the virtio drive UEFI has nothing to boot from on this
            # setup. Without the AHCI drive the kernel only sees virtio-blk,
            # which is not available early enough for the boot volume.
            if rpi_mode:
                # Raspberry Pi emulation mode (cortex-a72, no KVM)
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", smp_arg, *numa_args,
                    "-device", "ramfb",
                    *arm64_usb_devices,
                    "-machine", machine_arg,
                    "-cpu", "cortex-a72",
                    "-m", QEMU_ARM64_MEMORY,
                    "-bios", "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
                    *(qemu_arm64_iso_drive_args(img_path) if is_iso_boot else qemu_arm64_disk_drive_args(img_path)),
                    "-display", "none",
                    "-serial", "stdio"
                ]
            else:
                # Default accelerated mode
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", smp_arg, *numa_args,
                    "-device", "ramfb",
                    *arm64_usb_devices,
                    "-machine", machine_arg,
                    "-cpu", "max",
                    "-m", QEMU_ARM64_MEMORY,
                    "-bios", "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
                    *(qemu_arm64_iso_drive_args(img_path) if is_iso_boot else qemu_arm64_disk_drive_args(img_path)),
                    "-display", "none",
                    "-serial", f"file:{LOG_FILE}"
                ]

        add_qemu_control_monitor(qemu_cmd)
        add_qemu_gdb_server(qemu_cmd)
        print(f"  Command: {' '.join(qemu_cmd)}")

        try:
            if rpi_mode:
                # RPI mode: serial goes to stdio, redirect stdout to log file
                log_fd = open(LOG_FILE, 'w')
                qemu_process = subprocess.Popen(
                    qemu_cmd,
                    cwd=BUILD_DIR,
                    stdout=log_fd,
                    stderr=subprocess.STDOUT,
                    stdin=subprocess.DEVNULL
                )
            else:
                # Other ARM64 modes: serial goes directly to file via QEMU
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
    # i386 uses BIOS boot (no UEFI needed), amd64 uses UEFI
    use_uefi = (target_arch != "i386")
    ovmf_code = None
    ovmf_vars = None

    # Select architecture-specific settings
    qemu_binary = "qemu-system-i386" if target_arch == "i386" else "qemu-system-x86_64"
    is_darwin = platform.system() == "Darwin"
    darwin_amd64_simple = use_uefi and is_darwin and target_arch == "amd64"

    if use_uefi:
        ovmf_code, ovmf_vars, ovmf_vars_template, code_candidates, vars_candidates = resolve_ovmf_paths(target_arch)

    print(f"Starting QEMU ({target_arch}) with xHCI USB...")
    print(f"  QEMU binary: {qemu_binary}")
    print(f"  Boot mode: {'UEFI' if use_uefi else 'BIOS'}")
    if use_uefi:
        print(f"  OVMF CODE: {ovmf_code}")
        if darwin_amd64_simple:
            print("  OVMF VARS: (not used on macOS amd64)")
        else:
            print(f"  OVMF VARS: {ovmf_vars}")
    print(f"  {'ISO image' if is_iso_boot else 'Disk image'}: {img_path}")
    if not is_iso_boot:
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

        if use_uefi:
            # Open log file for stdout redirection (UEFI uses serial stdio)
            log_fd = open(LOG_FILE, 'w')
            if darwin_amd64_simple:
                # macOS amd64: Homebrew EDK2 code-only
                qemu_cmd = [
                    qemu_binary,
                    "-M", "q35",
                    "-m", "3G",
                    *(qemu_iso_drive_args(img_path) if is_iso_boot else [
                        "-drive", f"file={img_path}",
                    ]),
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
                    "-smp", smp_arg, *numa_args,
                    "-m", "3G",
                    "-M", "q35",
                    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
                    *(qemu_ahci_iso_args(img_path) if is_iso_boot else [
                        "-drive", f"file={img_path}",
                    ]),
                    "-serial", "stdio",
                    "-no-reboot",
                    "-no-shutdown",
                    "-device", "qemu-xhci,id=xhci",
                    "-device", "usb-kbd,bus=xhci.0",
                    "-device", "usb-mouse,bus=xhci.0",
                    *([] if is_iso_boot else [
                        "-drive", f"if=none,id=usbdisk,format=raw,file={FAT32_IMG}",
                        "-device", "usb-storage,bus=xhci.0,drive=usbdisk",
                    ])
                ]
        else:
            # BIOS boot for i386
            qemu_cmd = [
                qemu_binary,
                "-M", "q35",
                "-m", "3G",
                *(qemu_ahci_iso_args(img_path) if is_iso_boot else [
                    "-drive", f"file={img_path}",
                ]),
                "-serial", f"file:{LOG_FILE}",
                "-device", "qemu-xhci,id=xhci",
                "-device", "usb-kbd,bus=xhci.0",
                "-device", "usb-mouse,bus=xhci.0",
                *([] if is_iso_boot else [
                    "-drive", f"if=none,id=usbdisk,format=raw,file={FAT32_IMG}",
                    "-device", "usb-storage,bus=xhci.0,drive=usbdisk",
                ])
            ]

        # Add acceleration: TCG on macOS, KVM on Linux
        if is_darwin:
            qemu_cmd.insert(1, "-accel")
            qemu_cmd.insert(2, "tcg")
        else:
            qemu_cmd.insert(1, "-enable-kvm")

        add_qemu_control_monitor(qemu_cmd)
        add_qemu_gdb_server(qemu_cmd)
        print(f"  Command: {' '.join(qemu_cmd)}")

        if use_uefi:
            # UEFI mode: serial goes to stdio, redirect to log file
            qemu_process = subprocess.Popen(
                qemu_cmd,
                cwd=BUILD_DIR,
                stdout=log_fd,
                stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL
            )
        else:
            # BIOS mode: serial goes directly to file via QEMU
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


def start_vm(rpi_mode=False, smp=4):
    """Start the VM (QEMU or VirtualBox)."""
    if use_qemu:
        return start_qemu(rpi_mode, smp=smp)
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


def append_to_log(filepath, message):
    try:
        with open(filepath, 'a') as f:
            f.write(f"\n{'='*60}\n")
            f.write(f"{message}\n")
            f.write(f"{'='*60}\n")
        return True
    except Exception:
        return False


def read_log_tail(filepath, max_bytes=65536):
    """Return the end of a text log without loading very large logs fully."""
    try:
        with open(filepath, 'rb') as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            f.seek(max(0, size - max_bytes), os.SEEK_SET)
            return f.read().decode(errors="ignore")
    except Exception:
        return ""


def log_has_crash_marker(log_text):
    """Return True if the log tail shows a debugger stop/crash/assertion."""
    return any(marker in log_text for marker in CRASH_LOG_MARKERS)


def extract_interesting_log_addresses(log_text, limit=12):
    """Extract kernel addresses from crash log fields that are worth symbolizing."""
    patterns = (
        ("caller", r'\bcaller=([0-9A-Fa-f]{8,16})'),
        ("pc", r'\bPC\s+([0-9A-Fa-f]{8,16})'),
        ("lr", r'\bLR\s+([0-9A-Fa-f]{8,16})'),
        ("elr", r'\bELR=([0-9A-Fa-f]{8,16})'),
        ("lr", r'\blr=([0-9A-Fa-f]{8,16})'),
        ("fault", r'\bAddress\s+([0-9A-Fa-f]{8,16})\s+base'),
    )
    seen = set()
    addresses = []

    for label, pattern in patterns:
        for match in re.finditer(pattern, log_text):
            value = int(match.group(1), 16)
            if value < 0xffff000000000000 or value in seen:
                continue
            seen.add(value)
            addresses.append((label, value))
            if len(addresses) >= limit:
                return addresses

    return addresses


def find_binary(module_name):
    """Find the binary file for a given module name."""
    # Remove extension for directory matching
    base_name = os.path.splitext(module_name)[0]
    module_lower = module_name.lower()

    # Search paths
    search_paths = [
        os.path.join(BUILD_DIR, f"symbols/{module_name}"),
        os.path.join(BUILD_DIR, f"symbols/{module_lower}"),
        os.path.join(BUILD_DIR, f"{base_name}/{module_name}"),
        os.path.join(BUILD_DIR, f"ntoskrnl/{module_name}"),
        os.path.join(BUILD_DIR, f"win32ss/{module_name}"),
        os.path.join(BUILD_DIR, f"drivers/win32k/{module_name}"),
        os.path.join(BUILD_DIR, f"drivers/{base_name}/{module_name}"),
        os.path.join(BUILD_DIR, module_name),
    ]

    for path in search_paths:
        if os.path.exists(path):
            return path

    return None


def find_debug_binary(module_name):
    """Find the split debug file for a module, if the build produced one."""
    module_lower = module_name.lower()
    candidates = [
        os.path.join(BUILD_DIR, "symbols", module_name),
        os.path.join(BUILD_DIR, "symbols", module_lower),
    ]

    for path in candidates:
        if os.path.exists(path):
            return path

    return None


def find_gdb_symbol_binary(module_name):
    """Find the PE image that GDB can relocate and symbolize."""
    base_name = os.path.splitext(module_name)[0]
    candidates = [
        os.path.join(BUILD_DIR, base_name, module_name),
        os.path.join(BUILD_DIR, module_name),
    ]

    for path in candidates:
        if os.path.exists(path):
            return path

    return find_binary(module_name) or find_debug_binary(module_name)


# Global symbol cache: module_name -> list of (rva, name) sorted by rva
_symbol_cache = {}
_image_base_cache = {}
_section_vma_cache = {}
_tool_cache = {}


def _tool_sort_key(path):
    """Sort LLVM tool paths by version descending."""
    match = re.search(r'/llvm-(\d+)/', path)
    if match:
        return int(match.group(1))
    return 0


def find_tool(tool_name):
    """Find an executable tool path with LLVM versioned fallbacks."""
    if tool_name in _tool_cache:
        return _tool_cache[tool_name]

    candidates = [tool_name]
    for version in (21, 20, 19, 18, 17, 16, 15, 14, 13, 12):
        candidates.append(f"{tool_name}-{version}")

    for candidate in candidates:
        tool_path = shutil.which(candidate)
        if tool_path:
            _tool_cache[tool_name] = tool_path
            return tool_path

    globbed = sorted(
        glob.glob(f"/usr/lib/llvm-*/bin/{tool_name}"),
        key=_tool_sort_key,
        reverse=True
    )
    if globbed:
        _tool_cache[tool_name] = globbed[0]
        return globbed[0]

    _tool_cache[tool_name] = None
    return None


def get_image_base(binary_path):
    """Get ImageBase from PE header using llvm-readobj."""
    if binary_path in _image_base_cache:
        return _image_base_cache[binary_path]

    # Try llvm-readobj
    try:
        llvm_readobj = find_tool('llvm-readobj')
        if not llvm_readobj:
            raise FileNotFoundError("llvm-readobj not found")
        result = subprocess.run(
            [llvm_readobj, '--file-headers', binary_path],
            capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            for line in result.stdout.split('\n'):
                if 'ImageBase:' in line:
                    match = re.search(r'ImageBase:\s*(0x[0-9A-Fa-f]+|\d+)', line)
                    if match:
                        val = match.group(1)
                        base = int(val, 16) if val.startswith('0x') else int(val)
                        _image_base_cache[binary_path] = base
                        return base
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass

    # Fallback to known values
    known = {'ntoskrnl.exe': 0x400000, 'win32k.sys': 0x10000}
    base = known.get(os.path.basename(binary_path), 0x10000)
    _image_base_cache[binary_path] = base
    return base


def get_section_vma(binary_path, section_name):
    """Return the preferred VMA for a PE section using llvm-readobj."""
    cache_key = (binary_path, section_name)
    if cache_key in _section_vma_cache:
        return _section_vma_cache[cache_key]

    llvm_readobj = find_tool('llvm-readobj')
    if not llvm_readobj:
        return None

    try:
        result = subprocess.run(
            [llvm_readobj, '--sections', binary_path],
            capture_output=True, text=True, timeout=5
        )
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None

    if result.returncode != 0:
        return None

    image_base = get_image_base(binary_path)
    current_name = None
    for line in result.stdout.splitlines():
        name_match = re.search(r'Name:\s+([^ ]+)', line)
        if name_match:
            current_name = name_match.group(1)
            continue

        if current_name == section_name:
            va_match = re.search(r'VirtualAddress:\s+(0x[0-9A-Fa-f]+|\d+)', line)
            if va_match:
                value = va_match.group(1)
                section_vma = image_base + (int(value, 16) if value.startswith('0x') else int(value))
                _section_vma_cache[cache_key] = section_vma
                return section_vma

    return None


def get_symbol_vma(binary_path, symbol_name):
    """Return the preferred VMA for a symbol using llvm-nm."""
    llvm_nm = find_tool('llvm-nm')
    if not llvm_nm:
        return None

    try:
        result = subprocess.run(
            [llvm_nm, '--defined-only', '-n', binary_path],
            capture_output=True, text=True, timeout=30
        )
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None

    if result.returncode != 0:
        return None

    for line in result.stdout.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3 and parts[2] == symbol_name:
            try:
                return int(parts[0], 16)
            except ValueError:
                return None

    return None


def load_symbols(module_name):
    """Load symbols for a module using llvm-nm --defined-only -n."""
    if module_name in _symbol_cache:
        return _symbol_cache[module_name]

    binary_path = find_binary(module_name)
    if not binary_path:
        _symbol_cache[module_name] = []
        return []

    image_base = get_image_base(binary_path)

    try:
        llvm_nm = find_tool('llvm-nm')
        if not llvm_nm:
            raise FileNotFoundError("llvm-nm not found")
        result = subprocess.run(
            [llvm_nm, '--defined-only', '-n', binary_path],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode != 0:
            _symbol_cache[module_name] = []
            return []

        symbols = []
        for line in result.stdout.split('\n'):
            line = line.strip()
            if not line:
                continue
            parts = line.split(None, 2)
            if len(parts) >= 3:
                vma_str, sym_type, name = parts
                # Only include text/function symbols, skip data/absolute/section
                if sym_type not in ('T', 't'):
                    continue
                # Skip ARM mapping symbols ($x, $d, $t)
                if name.startswith('$'):
                    continue
                try:
                    vma = int(vma_str, 16)
                    rva = vma - image_base
                    if rva >= 0:
                        symbols.append((rva, name))
                except ValueError:
                    continue

        symbols.sort(key=lambda x: x[0])
        _symbol_cache[module_name] = symbols
        if symbols:
            print(f"  Loaded {len(symbols)} symbols for {module_name} from {binary_path}")
        return symbols

    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        print(f"  Warning: Could not load symbols for {module_name}: {e}")
        _symbol_cache[module_name] = []
        return []


def lookup_symbol(module_name, offset, symbols=None):
    """Look up the nearest symbol for a given RVA offset using bisect."""
    if symbols is None:
        symbols = load_symbols(module_name)
    if not symbols:
        return None

    rvas = [s[0] for s in symbols]
    idx = bisect.bisect_right(rvas, offset) - 1

    if idx < 0:
        return None

    rva, name = symbols[idx]
    delta = offset - rva

    # If too far from any symbol, likely noise
    if delta > 0x10000:
        return None

    if delta == 0:
        return name
    return f"{name}+0x{delta:x}"


def translate_address_with_addr2line(binary_path, offset):
    """Use llvm-addr2line to translate an address to function name and source location."""
    try:
        llvm_addr2line = find_tool('llvm-addr2line')
        if not llvm_addr2line:
            raise FileNotFoundError("llvm-addr2line not found")
        result = subprocess.run(
            [llvm_addr2line, '-e', binary_path, '-f', '-C', hex(offset)],
            capture_output=True,
            text=True,
            timeout=2
        )
        if result.returncode == 0:
            lines = result.stdout.strip().split('\n')
            if len(lines) >= 2 and lines[0] != '??':
                func_name = lines[0]
                source_loc = lines[1] if len(lines) > 1 else '??:0'
                return func_name, source_loc
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass

    return None, None


def get_function_name(module, offset):
    """Get function name based on known offsets."""
    # Specific known functions (from manual analysis)
    specific_functions = {
        'ntoskrnl.exe': {
            0x15dee0: 'ExLockHandleTableEntry',
            0x9a2c8: 'ExAllocatePoolWithTag',
            0x9ad78: 'ExAllocatePoolWithTag',
            0x9b508: 'ExAllocatePool',
            0x15a7d0: 'MiAllocatePoolPages',
            0x9ec0c: 'KiSystemCallEntry64',
            0x9ed44: 'KiSystemServiceRepeat',
            0x42ec: 'KiExceptionDispatch',
            0x187b18: 'ObpCreateHandle',
            0x34c0c: 'ObCreateObject',
            0x318ec: 'ObpAllocateObject',
            0x982dc: 'MiAllocateWsle',
            0x15d628: 'MiAllocatePoolPages',
            0x186b90: 'ExpAllocateHandleTableEntry',
            0x186cc0: 'ExpAllocateHandleTableEntrySlow',
            0x186e58: 'ExpAllocateLowLevelTable',
            0x13df78: 'KiSystemServiceCopyEnd',
            0x1605bc: 'KiSystemServiceRepeat',
            0x15f1b4: 'KiFastCallEntry',
            0x6830: 'KiExceptionDispatchContinue',
            0xfe148: 'MmTrimUserMemory',
            0x187ad8: 'ObpCloseHandle',
            0x9ee48: 'KiSystemServiceHandler',
            0x1600f0: 'KiSystemServiceStart',
            0x160688: 'KiSystemServiceExit',
            0x10419c: 'NtClose',
            0xccf28: 'ObCloseHandle',
            0x104a5c: 'NtCreateFile',
            0xc55d0: 'IopCreateFile',
            0xefe2c: 'IopParseDevice',
            0xf0338: 'IopParseFile',
            0xf566c: 'ObpLookupObjectName',
            0x16016c: 'KiServiceExit2',
        },
        'win32k.sys': {
            0x1f178: 'NtUserCallOneParam',
            0x1f414: 'Win32kSystemServiceDispatch',
            0xc2108: 'UserGetProcessWindowStation',
            0xc2280: 'IntGetAndReferenceClass',
            0xc22c4: 'UserSetProcessWindowStation',
            0xc2360: 'CheckWinstaAttributeAccess',
            0xc2418: 'ONEPARAM_ROUTINE_SETPROCDEFLAYOUT',
        }
    }

    if module in specific_functions and offset in specific_functions[module]:
        return specific_functions[module][offset]

    return None


def sanitize_log_content(log_content):
    """Strip ANSI/control escape sequences to improve backtrace parsing."""
    content = re.sub(r'\x1B\[[0-?]*[ -/]*[@-~]', '', log_content)
    content = re.sub(r'\x1B[@-_]', '', content)
    content = content.replace('\u241b', '')
    return content


def parse_backtrace_frames(log_content):
    """Extract frame tuples as (frame_addr, module, offset, offset_text)."""
    module_offset_pattern = re.compile(
        r'<\s*([A-Za-z0-9_.-]+\.(?:exe|sys|dll))\s*:\s*(0x[0-9A-Fa-f]+|[0-9A-Fa-f]+)\s*>',
        re.IGNORECASE
    )
    frame_addr_pattern = re.compile(r'~?\[([0-9A-Fa-f]{8,16})\]')

    frames = []
    for raw_line in sanitize_log_content(log_content).splitlines():
        line = raw_line.strip()
        if not line:
            continue

        frame_match = frame_addr_pattern.search(line)
        frame_addr = frame_match.group(1) if frame_match else "????????????????"

        for mod_match in module_offset_pattern.finditer(line):
            module_name = mod_match.group(1).lower()
            offset_text = mod_match.group(2)
            try:
                offset_value = int(offset_text, 16)
            except ValueError:
                continue
            frames.append((frame_addr, module_name, offset_value, offset_text))

    return frames


def normalize_offset(module_name, offset, symbols):
    """Normalize module offset to an RVA if a VA was provided."""
    if not symbols:
        return offset

    max_rva = symbols[-1][0]
    if offset <= (max_rva + 0x10000):
        return offset

    binary_path = find_binary(module_name)
    if not binary_path:
        return offset

    image_base = get_image_base(binary_path)
    if offset < image_base:
        return offset

    candidate_rva = offset - image_base
    if 0 <= candidate_rva <= (max_rva + 0x10000):
        return candidate_rva

    return offset


def translate_backtrace(log_content):
    """
    Parse and translate backtraces found in the log content.
    Returns a translated backtrace string or None if no backtrace found.
    """
    frames = parse_backtrace_frames(log_content)
    if not frames:
        return None

    result = []
    result.append("\nBACKTRACE TRANSLATION")
    result.append("Backtrace:")

    for addr, module, offset, offset_text in frames:
        symbols = load_symbols(module)
        normalized = normalize_offset(module, offset, symbols)

        # Try llvm-nm symbols first, then hardcoded table
        sym = lookup_symbol(module, normalized, symbols)
        if not sym:
            sym = get_function_name(module, normalized)

        binary_path = find_binary(module)
        source = None
        if binary_path:
            image_base = get_image_base(binary_path)
            va = normalized + image_base
            _, source = translate_address_with_addr2line(binary_path, va)
            if source in ("??:0", "??:?"):
                source = None

        if sym:
            if source:
                result.append(f"  [{addr}] <{module}:{offset_text}> {sym} ({source})")
            else:
                result.append(f"  [{addr}] <{module}:{offset_text}> {sym}")
        else:
            result.append(f"  [{addr}] <{module}:{offset_text}>")

    result.append("")  # Empty line at end

    return "\n".join(result)


def check_and_translate_backtrace(filepath):
    """
    Check if log contains a backtrace and translate it if found.
    Appends translation to the log file.
    """
    try:
        with open(filepath, 'r', errors='ignore') as f:
            content = f.read()

        translation = translate_backtrace(content)
        if translation:
            # Check if we already added translation
            if "BACKTRACE TRANSLATION" not in content:
                append_to_log(filepath, translation)
                print("\n" + translation)
                return True
    except Exception as e:
        print(f"Error translating backtrace: {e}")

    return False


def gdb_candidates():
    """Return GDB executable candidates for the current target."""
    override = os.environ.get("ROS_GDB")
    candidates = []
    if override:
        candidates.append(override)

    if target_arch == "arm64":
        candidates.extend((
            "gdb-multiarch",
            "aarch64-elf-gdb",
            "aarch64-w64-mingw32-gdb",
            "gdb",
        ))
    else:
        candidates.extend(("gdb-multiarch", "gdb"))

    # Preserve order while avoiding duplicate probes.
    return list(dict.fromkeys(candidates))


def find_gdb():
    """Find a GDB that can talk to QEMU's remote stub."""
    for candidate in gdb_candidates():
        path = shutil.which(candidate)
        if path:
            return path
    return None


def capture_gdb_dump(reason):
    """Capture a concise live QEMU/GDB state dump on stall/timeout."""
    if not ENABLE_GDB_DUMP or not use_qemu:
        return False
    if not qemu_process or qemu_process.poll() is not None:
        return False

    gdb = find_gdb()
    if not gdb:
        print(f"GDB state dump skipped: no GDB CLI found. Tried: {', '.join(gdb_candidates())}")
        return False

    # The SMP boot loads /KERNEL=ntkrnlmp.exe, whose code layout differs from the
    # UP ntoskrnl.exe. Symbolizing an MP boot against ntoskrnl.exe gives wrong
    # function/instruction attributions, so prefer ntkrnlmp.exe when present.
    ntoskrnl_symbols = find_gdb_symbol_binary("ntkrnlmp.exe") or find_gdb_symbol_binary("ntoskrnl.exe")
    if not ntoskrnl_symbols:
        print("GDB state dump skipped: ntkrnlmp.exe/ntoskrnl.exe not found.")
        return False

    gdb_script = f"/tmp/vm_monitor_gdb_dump_{qemu_process.pid}.gdb"
    commands = [
        "set pagination off",
        "set confirm off",
        "set debuginfod enabled off",
    ]

    if target_arch == "arm64":
        text_vma = get_section_vma(ntoskrnl_symbols, ".text")
        commands.append("set architecture aarch64")
    else:
        text_vma = None

    commands.extend([
        f"target remote 127.0.0.1:{QEMU_GDB_PORT}",
    ])

    if target_arch == "arm64":
        log_addresses = extract_interesting_log_addresses(read_log_tail(LOG_FILE))
        symbols_loaded = False
        if KERNEL_TEXT_ADDRESS is not None and text_vma:
            commands.append(f"set $nt_text = {KERNEL_TEXT_ADDRESS:#x}")
            commands.append("p/x $nt_text")
            commands.append(f"set $nt_delta = $nt_text - {text_vma:#x}")
            add_symbol_parts = [f"add-symbol-file {ntoskrnl_symbols} $nt_text"]
            for section_name in (".rdata", ".data", ".pdata", ".evec", ".pvec", "INIT", "INITDATA", "PAGE"):
                section_vma = get_section_vma(ntoskrnl_symbols, section_name)
                if section_vma:
                    add_symbol_parts.append(f"-s {section_name} ($nt_delta+{section_vma:#x})")
            commands.append(" ".join(add_symbol_parts))
            symbols_loaded = True
        elif KERNEL_TEXT_ADDRESS is not None:
            print("GDB state dump: ARM64 symbol load skipped; could not derive ntoskrnl .text VMA.")
        else:
            print("GDB state dump: ARM64 symbol load skipped; set ROS_KERNEL_TEXT to the loaded kernel .text address.")

        if symbols_loaded and log_addresses:
            commands.append('printf "ARM64 serial address symbols:\\n"')
            for label, address in log_addresses:
                commands.append(f'printf "  {label} {address:#018x}: "')
                commands.append(f"info symbol {address:#x}")
    else:
        commands.append(f"symbol-file {ntoskrnl_symbols}")

    commands.extend([
        "monitor info status",
        "monitor info cpus",
    ])

    if target_arch == "arm64":
        commands.extend([
        'printf "ARM64 selected general registers:\\n"',
        "info registers pc sp x29 x30",
        "info symbol $pc",
        "info symbol $x30",
        "x/8i $pc",
        'printf "SMP4DBG per-CPU counters at stall. Layout per CPU = 14 u32:\\n"',
        'printf "  [Begin Eoi Reject Tick Ipi Park Wake Ctl Tval Pmr GicPrio GicEn GicPend GicAct]\\n"',
        "info address SmpDbgCpu",
        "x/112xw &SmpDbgCpu",
        'printf "GICv2 GICD ISPENDR0 / ISACTIVER0 (phys, INTID 0-31):\\n"',
        "monitor xp/1xw 0x08000200",
        "monitor xp/1xw 0x08000300",
        ])
    else:
        commands.extend([
        "info symbol $pc",
        "info symbol $sp",
        "x/8i $pc",
        ])

    commands.extend([
        "bt 24",
        "thread apply all bt 6",
        "detach",
        "quit",
    ])

    try:
        with open(gdb_script, "w") as f:
            f.write("\n".join(commands))
            f.write("\n")

        print(f"Capturing GDB state dump using {ntoskrnl_symbols}...")
        result = subprocess.run(
            [gdb, "-nx", "-q", "-batch", "-x", gdb_script],
            cwd=BUILD_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=12
        )

        output = result.stdout or ""
        dump = (
            f"GDB STATE DUMP ({reason})\n"
            f"Command file: {gdb_script}\n"
            f"Symbols: {ntoskrnl_symbols}\n"
            f"Return code: {result.returncode}\n\n"
            f"{output}"
        )
        append_to_log(LOG_FILE, dump)
        print("\n" + dump)
        return True
    except Exception as e:
        print(f"GDB state dump failed: {e}")
        return False


SMPSTAT_HEADER_RE = re.compile(
    r"\bSMPSTAT\s+sample=(\d+)\s+elapsed_ms=(\d+)\s+"
    r"active=(\S+)\s+idle=(\S+)"
)
SMPSTAT_CPU_RE = re.compile(r"\bSMPSTAT\s+cpu=(\d+)\b")
SMPSTAT_FIELD_RE = re.compile(r"\b([A-Za-z][A-Za-z0-9_]*)=(\S+)")
SMPSTAT_DELTA_FIELDS = (
    "dtick",
    "dqreq",
    "dqend",
    "ddisp",
    "dipi",
    "drdpc",
    "dsched",
    "ddpc",
    "dtb",
    "dgen",
    "dplace",
    "didle",
    "dquant",
    "dperiod",
    "dstandby",
    "dcharge",
    "dcycles",
    "dreset",
    "dsrc0",
    "dsrc1",
    "dsrc2",
    "dsrc3",
    "dbsrc0",
    "dbsrc1",
    "dbsrc2",
    "dbsrc3",
    "dcsw",
    "dpcq",
    "dpcact",
    "dpcpend",
)


def parse_smpstat_line(line):
    """Parse one SMPSTAT line into a sample header or per-CPU row."""
    line = sanitize_log_content(line)
    match = SMPSTAT_HEADER_RE.search(line)
    if match:
        try:
            active = int(match.group(3), 0)
            idle = int(match.group(4), 0)
        except ValueError:
            return None, None
        return "sample", {
            "sample": int(match.group(1)),
            "elapsed_ms": int(match.group(2)),
            "active": active,
            "idle": idle,
            "cpus": {},
        }

    match = SMPSTAT_CPU_RE.search(line)
    if not match:
        return None, None

    fields = dict(SMPSTAT_FIELD_RE.findall(line))
    fields["cpu"] = int(match.group(1))
    return "cpu", fields


def smpstat_int(row, field):
    """Return an integer SMPSTAT field, treating a missing/bad field as zero."""
    try:
        return int(row.get(field, "0"), 0)
    except (TypeError, ValueError):
        return 0


def smpstat_pointer(value):
    """Keep useful pointer identity while displaying a zero pointer compactly."""
    try:
        if int(value, 0) == 0:
            return "-"
    except (TypeError, ValueError):
        pass
    return str(value)


def format_smpstat_table(sample, max_elapsed_ms):
    """Render one SMPSTAT sample as a compact table with one row per CPU."""
    rows = sample["cpus"]
    expected = bin(sample["active"]).count("1")
    completeness = ""
    if expected and len(rows) != expected:
        completeness = f" rows={len(rows)}/{expected}"

    title = (
        f"SMPSTAT sample={sample['sample']} elapsed={sample['elapsed_ms']}ms "
        f"max={max_elapsed_ms}ms active={sample['active']:#x} "
        f"idle={sample['idle']:#x}{completeness}"
    )
    header = (
        f"{'CPU':>5} {'state':<5} {'tick':>5} {'qreq':>5} {'qend':>5} "
        f"{'disp':>5} {'ipi':>5} {'rdpc':>5} {'ctxsw':>6} {'ready':>7} "
        f"{'rdy#':>5} {'qf':>3} {'dpc(q/a/p)':>10} {'current':>16} {'next':>16}"
    )
    lines = [title, header, "-" * len(header)]
    totals = {field: 0 for field in SMPSTAT_DELTA_FIELDS}
    ready_total = 0

    for cpu, row in sorted(rows.items()):
        for field in SMPSTAT_DELTA_FIELDS:
            totals[field] += smpstat_int(row, field)
        ready_total |= smpstat_int(row, "ready")
        state = "idle" if sample["idle"] & (1 << cpu) else "run"
        dpc = (
            f"{smpstat_int(row, 'dpcq')}/"
            f"{smpstat_int(row, 'dpcact')}/"
            f"{smpstat_int(row, 'dpcpend')}"
        )
        lines.append(
            f"{cpu:>5} {state:<5} {smpstat_int(row, 'dtick'):>5} "
            f"{smpstat_int(row, 'dqreq'):>5} {smpstat_int(row, 'dqend'):>5} "
            f"{smpstat_int(row, 'ddisp'):>5} {smpstat_int(row, 'dipi'):>5} "
            f"{smpstat_int(row, 'drdpc'):>5} {smpstat_int(row, 'dcsw'):>6} "
            f"{row.get('ready', '0x0'):>7} {smpstat_int(row, 'rdycnt'):>5} "
            f"{smpstat_int(row, 'qflag'):>3} "
            f"{dpc:>10} {smpstat_pointer(row.get('cur', '0')):>16} "
            f"{smpstat_pointer(row.get('next', '0')):>16}"
        )

    total_dpc = (
        f"{totals['dpcq']}/{totals['dpcact']}/{totals['dpcpend']}"
    )
    lines.extend([
        "-" * len(header),
        f"{'TOTAL':>5} {'':<5} {totals['dtick']:>5} {totals['dqreq']:>5} "
        f"{totals['dqend']:>5} {totals['ddisp']:>5} {totals['dipi']:>5} "
        f"{totals['drdpc']:>5} {totals['dcsw']:>6} {ready_total:#7x} "
        f"{sum(smpstat_int(row, 'rdycnt') for row in rows.values()):>5} "
        f"{sum(smpstat_int(row, 'qflag') for row in rows.values()):>3} "
        f"{total_dpc:>10} {'-':>16} {'-':>16}",
    ])

    quantum_header = (
        f"{'CPU':>5} {'pri':>4} {'base':>5} {'state':>5} {'reset':>5} "
        f"{'left':>5} {'unit':>10} {'charge#':>8} {'charged cycles':>18} "
        f"{'base resets':>11} {'remaining cycles':>18} {'cycle':>18} "
        f"{'target':>18} {'start cycle':>18}"
    )
    lines.extend(["", "SMPSTAT QUANTUM STATE", quantum_header, "-" * len(quantum_header)])
    for cpu, row in sorted(rows.items()):
        cycle = smpstat_int(row, "cycle")
        target = smpstat_int(row, "qtarget")
        lines.append(
            f"{cpu:>5} {smpstat_int(row, 'curpri'):>4} "
            f"{smpstat_int(row, 'curbase'):>5} "
            f"{smpstat_int(row, 'curstate'):>5} "
            f"{smpstat_int(row, 'qreset'):>5} "
            f"{smpstat_int(row, 'qleft'):>5} "
            f"{smpstat_int(row, 'qunit'):>10} "
            f"{smpstat_int(row, 'dcharge'):>8} "
            f"{smpstat_int(row, 'dcycles'):>18} "
            f"{smpstat_int(row, 'dreset'):>11} "
            f"{target - cycle:>18} {cycle:>18} {target:>18} "
            f"{smpstat_int(row, 'startcyc'):>18}"
        )

    ownership_header = (
        f"{'CPU':>5} {'current image':<16} {'pid':>8} {'tid':>8} "
        f"{'pri':>4} {'affinity':>10} {'ideal':>5} {'cpu':>4} "
        f"{'next image':<16} {'pid':>8} {'tid':>8} {'pri':>4} "
        f"{'affinity':>10} {'ideal':>5} {'cpu':>4}"
    )
    lines.extend(["", "SMPSTAT DISPATCH OWNERSHIP", ownership_header, "-" * len(ownership_header)])
    for cpu, row in sorted(rows.items()):
        lines.append(
            f"{cpu:>5} {row.get('curimg', '-'):<16.16} "
            f"{row.get('curpid', '0'):>8} {row.get('curtid', '0'):>8} "
            f"{smpstat_int(row, 'curpri'):>4} "
            f"{row.get('curaff', '0x0'):>10} "
            f"{smpstat_int(row, 'curideal'):>5} "
            f"{smpstat_int(row, 'curcpu'):>4} "
            f"{row.get('nextimg', '-'):<16.16} "
            f"{row.get('nextpid', '0'):>8} {row.get('nexttid', '0'):>8} "
            f"{smpstat_int(row, 'nextpri'):>4} "
            f"{row.get('nextaff', '0x0'):>10} "
            f"{smpstat_int(row, 'nextideal'):>5} "
            f"{smpstat_int(row, 'nextcpu'):>4}"
        )

    ready_header = (
        f"{'CPU':>5} {'ready':>7} {'rdy#':>5} {'head':>16} "
        f"{'head image':<16} {'pid':>8} {'tid':>8} {'pri':>4} "
        f"{'state':>5} {'age':>10} {'affinity':>10} {'ideal':>5} {'cpu':>4}"
    )
    lines.extend(["", "SMPSTAT READY HEAD", ready_header, "-" * len(ready_header)])
    for cpu, row in sorted(rows.items()):
        lines.append(
            f"{cpu:>5} {row.get('ready', '0x0'):>7} "
            f"{smpstat_int(row, 'rdycnt'):>5} "
            f"{smpstat_pointer(row.get('rdythr', '0')):>16} "
            f"{row.get('rdyimg', '-'):<16.16} "
            f"{row.get('rdypid', '0'):>8} {row.get('rdytid', '0'):>8} "
            f"{smpstat_int(row, 'rdypri'):>4} "
            f"{smpstat_int(row, 'rdystate'):>5} "
            f"{smpstat_int(row, 'rdyage'):>10} "
            f"{row.get('rdyaff', '0x0'):>10} "
            f"{smpstat_int(row, 'rdyideal'):>5} "
            f"{smpstat_int(row, 'rdycpu'):>4}"
        )

    ipi_header = (
        f"{'CPU':>5} {'sched':>7} {'dpcq_ipi':>8} {'tb':>7} {'generic':>7} "
        f"{'from0':>7} {'from1':>7} {'from2':>7} {'from3':>7} "
        f"{'last image':<16} {'pid':>8} {'tid':>8} {'src':>4} "
        f"{'pri':>4} {'ideal':>5} {'cause':>5}"
    )
    lines.extend(["", "SMPSTAT IPI ATTRIBUTION", ipi_header, "-" * len(ipi_header)])
    for cpu, row in sorted(rows.items()):
        lines.append(
            f"{cpu:>5} {smpstat_int(row, 'dsched'):>7} "
            f"{smpstat_int(row, 'ddpc'):>8} "
            f"{smpstat_int(row, 'dtb'):>7} "
            f"{smpstat_int(row, 'dgen'):>7} "
            f"{smpstat_int(row, 'dsrc0'):>7} "
            f"{smpstat_int(row, 'dsrc1'):>7} "
            f"{smpstat_int(row, 'dsrc2'):>7} "
            f"{smpstat_int(row, 'dsrc3'):>7} "
            f"{row.get('lastimg', '-'):<16.16} "
            f"{row.get('lastpid', '0'):>8} {row.get('lasttid', '0'):>8} "
            f"{smpstat_int(row, 'lastsrc'):>4} "
            f"{smpstat_int(row, 'lastpri'):>4} "
            f"{smpstat_int(row, 'lastideal'):>5} "
            f"{smpstat_int(row, 'lastcause'):>5}"
        )
    lines.extend([
        "-" * len(ipi_header),
        f"{'TOTAL':>5} {totals['dsched']:>7} {totals['ddpc']:>8} "
        f"{totals['dtb']:>7} {totals['dgen']:>7} "
        f"{totals['dsrc0']:>7} {totals['dsrc1']:>7} "
        f"{totals['dsrc2']:>7} {totals['dsrc3']:>7}",
    ])

    balance_header = (
        f"{'CPU':>5} {'place':>8} {'idle':>8} {'quantum':>8} {'periodic':>9} {'standby':>9} "
        f"{'from0':>8} {'from1':>8} {'from2':>8} {'from3':>8}"
    )
    lines.extend(["", "SMPSTAT LOAD BALANCE", balance_header, "-" * len(balance_header)])
    for cpu, row in sorted(rows.items()):
        lines.append(
            f"{cpu:>5} {smpstat_int(row, 'dplace'):>8} "
            f"{smpstat_int(row, 'didle'):>8} "
            f"{smpstat_int(row, 'dquant'):>8} "
            f"{smpstat_int(row, 'dperiod'):>9} "
            f"{smpstat_int(row, 'dstandby'):>9} "
            f"{smpstat_int(row, 'dbsrc0'):>8} "
            f"{smpstat_int(row, 'dbsrc1'):>8} "
            f"{smpstat_int(row, 'dbsrc2'):>8} "
            f"{smpstat_int(row, 'dbsrc3'):>8}"
        )
    lines.extend([
        "-" * len(balance_header),
        f"{'TOTAL':>5} {totals['dplace']:>8} {totals['didle']:>8} "
        f"{totals['dquant']:>8} {totals['dperiod']:>9} {totals['dstandby']:>9} "
        f"{totals['dbsrc0']:>8} {totals['dbsrc1']:>8} "
        f"{totals['dbsrc2']:>8} {totals['dbsrc3']:>8}",
    ])
    return "\n".join(lines)


class SmpStatTablePrinter:
    """Collect SMPSTAT blocks and print live and whole-run tables."""

    def __init__(self, output=print):
        self.output = output
        self.pending = None
        self.max_elapsed_ms = 0
        self.sample_count = 0
        self.cpu_totals = {}
        self.finished = False

    def feed(self, line):
        markers = list(re.finditer(r"\bSMPSTAT\s+(?:sample|cpu)=", line))
        for index, marker in enumerate(markers):
            end = markers[index + 1].start() if index + 1 < len(markers) else len(line)
            self._feed_record(line[marker.start():end])

    def _feed_record(self, line):
        kind, data = parse_smpstat_line(line)
        if kind == "sample":
            self.flush()
            self.pending = data
            self.max_elapsed_ms = max(
                self.max_elapsed_ms, data["elapsed_ms"]
            )
            if data["active"] == 0:
                self.flush()
            return

        if kind != "cpu" or self.pending is None:
            return

        self.pending["cpus"][data["cpu"]] = data
        expected = bin(self.pending["active"]).count("1")
        if expected and len(self.pending["cpus"]) >= expected:
            self.flush()

    def flush(self):
        if self.pending is None:
            return

        sample = self.pending
        self.pending = None
        if not sample["cpus"]:
            return

        self._record(sample)
        self.output(format_smpstat_table(sample, self.max_elapsed_ms))
        self.output("")

    def _record(self, sample):
        self.sample_count += 1
        elapsed_ms = sample["elapsed_ms"]
        for cpu, row in sample["cpus"].items():
            totals = self.cpu_totals.setdefault(cpu, {
                "samples": 0,
                "elapsed_ms": 0,
                "idle_samples": 0,
                "max_ctxsw": 0,
                "last_ready": "0x0",
                **{field: 0 for field in SMPSTAT_DELTA_FIELDS},
            })
            totals["samples"] += 1
            totals["elapsed_ms"] += elapsed_ms
            totals["idle_samples"] += bool(sample["idle"] & (1 << cpu))
            totals["max_ctxsw"] = max(
                totals["max_ctxsw"], smpstat_int(row, "dcsw")
            )
            totals["last_ready"] = row.get("ready", "0x0")
            for field in SMPSTAT_DELTA_FIELDS:
                totals[field] += smpstat_int(row, field)

    def format_summary(self):
        if not self.cpu_totals:
            return ""

        header = (
            f"{'CPU':>5} {'samples':>7} {'idle_snap%':>10} {'tick/s':>8} "
            f"{'qreq/s':>8} {'qend/s':>8} {'disp/s':>8} {'ipi/s':>8} "
            f"{'rdpc/s':>8} {'ctxsw/s':>9} {'maxsw':>6} {'ready':>7}"
        )
        lines = [
            f"SMPSTAT RUN SUMMARY samples={self.sample_count} "
            f"worst_interval={self.max_elapsed_ms}ms",
            header,
            "-" * len(header),
        ]
        rate_fields = (
            "dtick",
            "dqreq",
            "dqend",
            "ddisp",
            "dipi",
            "drdpc",
            "dcsw",
        )
        for cpu, totals in sorted(self.cpu_totals.items()):
            seconds = totals["elapsed_ms"] / 1000.0
            rates = [
                totals[field] / seconds if seconds else 0.0
                for field in rate_fields
            ]
            idle_pct = (
                100.0 * totals["idle_samples"] / totals["samples"]
                if totals["samples"] else 0.0
            )
            lines.append(
                f"{cpu:>5} {totals['samples']:>7} {idle_pct:>9.1f}% "
                f"{rates[0]:>8.1f} {rates[1]:>8.1f} {rates[2]:>8.1f} "
                f"{rates[3]:>8.1f} {rates[4]:>8.1f} {rates[5]:>8.1f} "
                f"{rates[6]:>9.1f} {totals['max_ctxsw']:>6} "
                f"{totals['last_ready']:>7}"
            )
        quantum_header = (
            f"{'CPU':>5} {'charges/s':>11} {'charged cycles/s':>18} "
            f"{'baseline resets':>15} {'qreq/charge':>12} {'qend/charge':>12}"
        )
        lines.extend(["", "SMPSTAT QUANTUM ACCOUNTING SUMMARY", quantum_header, "-" * len(quantum_header)])
        for cpu, totals in sorted(self.cpu_totals.items()):
            seconds = totals["elapsed_ms"] / 1000.0
            charges = totals["dcharge"]
            lines.append(
                f"{cpu:>5} "
                f"{(charges / seconds if seconds else 0.0):>11.1f} "
                f"{(totals['dcycles'] / seconds if seconds else 0.0):>18.1f} "
                f"{totals['dreset']:>15} "
                f"{(totals['dqreq'] / charges if charges else 0.0):>12.3f} "
                f"{(totals['dqend'] / charges if charges else 0.0):>12.3f}"
            )
        ipi_header = (
            f"{'CPU':>5} {'sched/s':>9} {'dpcq_ipi/s':>11} {'tb/s':>9} {'generic/s':>11} "
            f"{'from0/s':>9} {'from1/s':>9} {'from2/s':>9} {'from3/s':>9}"
        )
        lines.extend(["", "SMPSTAT IPI ATTRIBUTION SUMMARY", ipi_header, "-" * len(ipi_header)])
        for cpu, totals in sorted(self.cpu_totals.items()):
            seconds = totals["elapsed_ms"] / 1000.0
            rates = [
                totals[field] / seconds if seconds else 0.0
                for field in (
                    "dsched", "ddpc", "dtb", "dgen", "dsrc0", "dsrc1", "dsrc2", "dsrc3"
                )
            ]
            lines.append(
                f"{cpu:>5} {rates[0]:>9.1f} {rates[1]:>11.1f} "
                f"{rates[2]:>9.1f} {rates[3]:>11.1f} "
                f"{rates[4]:>9.1f} {rates[5]:>9.1f} "
                f"{rates[6]:>9.1f} {rates[7]:>9.1f}"
            )
        balance_header = (
            f"{'CPU':>5} {'place/s':>10} {'idle/s':>10} {'quantum/s':>10} {'periodic/s':>11} {'standby/s':>11} "
            f"{'from0/s':>10} {'from1/s':>10} {'from2/s':>10} {'from3/s':>10}"
        )
        lines.extend(["", "SMPSTAT LOAD BALANCE SUMMARY", balance_header, "-" * len(balance_header)])
        for cpu, totals in sorted(self.cpu_totals.items()):
            seconds = totals["elapsed_ms"] / 1000.0
            rates = [
                totals[field] / seconds if seconds else 0.0
                for field in (
                    "dplace", "didle", "dquant", "dperiod", "dstandby",
                    "dbsrc0", "dbsrc1", "dbsrc2", "dbsrc3"
                )
            ]
            lines.append(
                f"{cpu:>5} {rates[0]:>10.1f} {rates[1]:>10.1f} "
                f"{rates[2]:>10.1f} {rates[3]:>11.1f} "
                f"{rates[4]:>11.1f} {rates[5]:>10.1f} "
                f"{rates[6]:>10.1f} {rates[7]:>10.1f} "
                f"{rates[8]:>10.1f}"
            )
        return "\n".join(lines)

    def finish(self):
        if self.finished:
            return
        self.finished = True
        self.flush()
        summary = self.format_summary()
        if summary:
            self.output(summary)


SERIAL_TIMESTAMP_RE = re.compile(r"\[\s*(\d+\.\d+)\]\s*")
TASKMGR11_BEGIN_RE = re.compile(r"\bTASKMGR11_DIAG_BEGIN\b")
TASKMGR11_SAMPLE_RE = re.compile(r"\bTASKMGR11_SAMPLE\b")
TASKMGR11_GRAPH_RE = re.compile(r"\bTASKMGR11_GRAPH\b")
CPUBENCH_BEGIN_RE = re.compile(r"\[cpubench\]\s+(.+?)\s+SMP_BEGIN\b")
CPUBENCH_END_RE = re.compile(r"\[cpubench\]\s+(.+?)\s+SMP_END\b")


class TaskMgrCadencePrinter:
    """Summarize Taskmgr11 data and graph cadence around each all-core burst."""

    def __init__(self, output=print):
        self.output = output
        self.period_ms = 0
        self.origin_qpc = 0
        self.frequency = 0
        self.origin_tick_ms = 0
        self.origin_serial_s = None
        self.samples = []
        self.graphs = {}
        self.pending_benchmarks = {}
        self.benchmarks = []
        self.finished = False

    @staticmethod
    def _serial_seconds(line, marker_position):
        timestamps = list(SERIAL_TIMESTAMP_RE.finditer(line, 0, marker_position))
        return float(timestamps[-1].group(1)) if timestamps else None

    def feed(self, line):
        match = TASKMGR11_BEGIN_RE.search(line)
        if match:
            fields = dict(SMPSTAT_FIELD_RE.findall(line[match.start():]))
            self.period_ms = int(fields.get("period_ms", "0"))
            self.origin_qpc = int(fields.get("qpc", "0"))
            self.frequency = int(fields.get("qpc_hz", "0"))
            self.origin_tick_ms = int(fields.get("tick_ms", "0"))
            self.origin_serial_s = self._serial_seconds(line, match.start())
            return

        match = TASKMGR11_SAMPLE_RE.search(line)
        if match:
            fields = dict(SMPSTAT_FIELD_RE.findall(line[match.start():]))
            self.samples.append({
                "seq": int(fields["seq"]),
                "qpc": int(fields.get("qpc", "0")),
                "tick_ms": int(fields.get("tick_ms", "0")),
                "serial_s": self._serial_seconds(line, match.start()),
                "at_ms": int(fields["at_ms"]),
                "delta_ms": int(fields["delta_ms"]),
                "late_ms": int(fields["late_ms"]),
                "query_ms": int(fields["query_ms"]),
                "cpu_pct": float(fields["cpu_pct"]),
            })
            return

        match = TASKMGR11_GRAPH_RE.search(line)
        if match:
            fields = dict(SMPSTAT_FIELD_RE.findall(line[match.start():]))
            sequence = int(fields["seq"])
            self.graphs[sequence] = {
                "seq": sequence,
                "qpc": int(fields.get("qpc", "0")),
                "tick_ms": int(fields.get("tick_ms", "0")),
                "serial_s": self._serial_seconds(line, match.start()),
                "at_ms": int(fields["at_ms"]),
                "delta_ms": int(fields["delta_ms"]),
                "sample_to_paint_ms": int(fields["sample_to_paint_ms"]),
                "cpu_pct": float(fields["cpu_pct"]),
                "cpu0_pct": float(fields["cpu0_pct"]),
                "cpu1_pct": float(fields["cpu1_pct"]),
                "cpu2_pct": float(fields["cpu2_pct"]),
                "cpu3_pct": float(fields["cpu3_pct"]),
            }
            return

        match = CPUBENCH_BEGIN_RE.search(line)
        if match:
            fields = dict(SMPSTAT_FIELD_RE.findall(line[match.start():]))
            name = match.group(1)
            self.pending_benchmarks.setdefault(name, []).append({
                "name": name,
                "cores": int(fields["cores"]),
                "target_s": int(fields["target_s"]),
                "start_qpc": int(fields.get("qpc", "0")),
                "start_tick_ms": int(fields.get("tick_ms", "0")),
                "start_serial_s": self._serial_seconds(line, match.start()),
            })
            return

        match = CPUBENCH_END_RE.search(line)
        if match:
            fields = dict(SMPSTAT_FIELD_RE.findall(line[match.start():]))
            name = match.group(1)
            pending = self.pending_benchmarks.get(name, [])
            if pending:
                benchmark = pending.pop(0)
                benchmark["end_qpc"] = int(fields.get("qpc", "0"))
                benchmark["end_tick_ms"] = int(fields.get("tick_ms", "0"))
                benchmark["end_serial_s"] = self._serial_seconds(line, match.start())
                benchmark["elapsed_ms"] = int(fields["elapsed_ms"])
                self.benchmarks.append(benchmark)

    def _window(self, name, start, end, clock):
        samples = [sample for sample in self.samples if sample[clock] is not None and start <= sample[clock] <= end]
        graphs = [self.graphs[sample["seq"]] for sample in samples if sample["seq"] in self.graphs]
        sample_times = sorted(sample[clock] for sample in samples)
        gaps = [(current - previous) * 1000.0 for previous, current in zip(sample_times, sample_times[1:])]
        paint_lags = [(graph[clock] - sample[clock]) * 1000.0 for sample in samples for graph in [self.graphs.get(sample["seq"])] if graph and graph[clock] is not None]
        duration_ms = (end - start) * 1000.0
        expected = int(duration_ms / self.period_ms) if self.period_ms else 0
        return {
            "name": name,
            "duration_ms": duration_ms,
            "expected": expected,
            "samples": len(samples),
            "graphs": len(graphs),
            "avg_gap": sum(gaps) / len(gaps) if gaps else 0.0,
            "max_gap": max(gaps, default=0),
            "over_750": sum(gap > 750 for gap in gaps),
            "over_1000": sum(gap > 1000 for gap in gaps),
            "max_paint": max(paint_lags, default=0.0),
            "missing": len(samples) - len(graphs),
            "cpu_pct": sum((graph["cpu_pct"] for graph in graphs), 0.0) / len(graphs) if graphs else 0.0,
            "cpu0_pct": sum((graph["cpu0_pct"] for graph in graphs), 0.0) / len(graphs) if graphs else 0.0,
            "cpu1_pct": sum((graph["cpu1_pct"] for graph in graphs), 0.0) / len(graphs) if graphs else 0.0,
            "cpu2_pct": sum((graph["cpu2_pct"] for graph in graphs), 0.0) / len(graphs) if graphs else 0.0,
            "cpu3_pct": sum((graph["cpu3_pct"] for graph in graphs), 0.0) / len(graphs) if graphs else 0.0,
        }

    def format_summary(self):
        if not self.samples:
            return ""

        serial_clock = self.origin_serial_s is not None and all(sample["serial_s"] is not None for sample in self.samples) and all(benchmark["start_serial_s"] is not None and benchmark["end_serial_s"] is not None for benchmark in self.benchmarks)
        if serial_clock:
            clock = "serial_s"
            origin = self.origin_serial_s
            benchmarks = sorted(self.benchmarks, key=lambda row: row["start_serial_s"])
            last = max([sample["serial_s"] for sample in self.samples] + [graph["serial_s"] for graph in self.graphs.values() if graph["serial_s"] is not None])
            benchmark_start = "start_serial_s"
            benchmark_end = "end_serial_s"
            clock_name = "serial"
        elif self.origin_tick_ms and all(sample["tick_ms"] for sample in self.samples):
            clock = "tick_ms"
            origin = self.origin_tick_ms
            benchmarks = sorted(self.benchmarks, key=lambda row: row["start_tick_ms"])
            last = max([sample["tick_ms"] for sample in self.samples] + [graph["tick_ms"] for graph in self.graphs.values()])
            benchmark_start = "start_tick_ms"
            benchmark_end = "end_tick_ms"
            clock_name = "tick"
        elif self.frequency and self.origin_qpc:
            clock = "qpc"
            origin = self.origin_qpc / self.frequency
            benchmarks = sorted(self.benchmarks, key=lambda row: row["start_qpc"])
            last = max([sample["qpc"] for sample in self.samples] + [graph["qpc"] for graph in self.graphs.values()]) / self.frequency
            for sample in self.samples:
                sample["qpc"] /= self.frequency
            for graph in self.graphs.values():
                graph["qpc"] /= self.frequency
            benchmark_start = "start_qpc_seconds"
            benchmark_end = "end_qpc_seconds"
            for benchmark in benchmarks:
                benchmark[benchmark_start] = benchmark["start_qpc"] / self.frequency
                benchmark[benchmark_end] = benchmark["end_qpc"] / self.frequency
            clock_name = "qpc"
        else:
            return ""

        if clock == "tick_ms":
            origin /= 1000.0
            last /= 1000.0
            for sample in self.samples:
                sample[clock] /= 1000.0
            for graph in self.graphs.values():
                graph[clock] /= 1000.0
            for benchmark in benchmarks:
                benchmark[benchmark_start] /= 1000.0
                benchmark[benchmark_end] /= 1000.0

        windows = [self._window("whole run", origin, last, clock)]
        if benchmarks:
            windows.append(self._window("baseline", origin, benchmarks[0][benchmark_start], clock))
            for benchmark in benchmarks:
                windows.append(self._window(benchmark["name"], benchmark[benchmark_start], benchmark[benchmark_end], clock))
            windows.append(self._window("recovery", benchmarks[-1][benchmark_end], last, clock))

        cadence_header = f"{'window':<16} {'wall_s':>7} {'expect':>6} {'sample':>6} {'paint':>6} {'avg_gap':>8} {'max_gap':>8} {'>750':>5} {'>1000':>6} {'draw':>6} {'miss':>5}"
        lines = [
            f"TASKMGR11 CADENCE SUMMARY clock={clock_name} target={self.period_ms}ms samples={len(self.samples)} paints={len(self.graphs)}",
            cadence_header,
            "-" * len(cadence_header),
        ]
        for row in windows:
            lines.append(f"{row['name']:<16.16} {row['duration_ms'] / 1000.0:>7.2f} {row['expected']:>6} {row['samples']:>6} {row['graphs']:>6} {row['avg_gap']:>8.1f} {row['max_gap']:>8.1f} {row['over_750']:>5} {row['over_1000']:>6} {row['max_paint']:>6.1f} {row['missing']:>5}")

        cpu_header = f"{'window':<16} {'total%':>8} {'cpu0%':>8} {'cpu1%':>8} {'cpu2%':>8} {'cpu3%':>8}"
        lines.extend(["", "TASKMGR11 CPU GRAPH SUMMARY", cpu_header, "-" * len(cpu_header)])
        for row in windows:
            lines.append(f"{row['name']:<16.16} {row['cpu_pct']:>8.1f} {row['cpu0_pct']:>8.1f} {row['cpu1_pct']:>8.1f} {row['cpu2_pct']:>8.1f} {row['cpu3_pct']:>8.1f}")
        return "\n".join(lines)

    def finish(self):
        if self.finished:
            return
        self.finished = True
        summary = self.format_summary()
        if summary:
            self.output(summary)


class SerialLogFollower:
    """Feed complete lines appended to a serial log to a callback."""

    def __init__(self, filepath, callback):
        self.filepath = filepath
        self.callback = callback
        self.position = 0
        self.inode = None
        self.fragment = ""

    def read_available(self):
        try:
            stat = os.stat(self.filepath)
            if self.inode != stat.st_ino or stat.st_size < self.position:
                self.inode = stat.st_ino
                self.position = 0
                self.fragment = ""

            with open(self.filepath, "rb") as logfile:
                logfile.seek(self.position)
                chunk = logfile.read()
                self.position = logfile.tell()
        except OSError:
            return

        if not chunk:
            return

        text = self.fragment + chunk.decode(errors="replace")
        self.fragment = ""
        for line in text.splitlines(keepends=True):
            if line.endswith(("\n", "\r")):
                self.callback(line.rstrip("\r\n"))
            else:
                self.fragment = line

    def finish(self):
        self.read_available()
        if self.fragment:
            self.callback(self.fragment)
            self.fragment = ""


def monitor_log():
    """Monitor log file for stalls and enforce hard timeout."""
    global qemu_process, use_qemu

    stall_count = 0
    last_size = -1
    last_change_time = time.time()
    
    overall_start_time = time.time()

    print(f"Monitoring log file: {LOG_FILE}")
    print(f"Stall timeout: {STALL_TIMEOUT} seconds")
    print(f"Hard timeout: {HARD_TIMEOUT} seconds")
    if ENABLE_SMP_TABLE:
        print("SMPSTAT live tables: enabled")

    wait_count = 0
    while get_file_size(LOG_FILE) <= 0:
        if time.time() - overall_start_time > HARD_TIMEOUT:
            print(f"HARD TIMEOUT ({HARD_TIMEOUT}s) reached waiting for log.")
            capture_gdb_dump("hard timeout waiting for serial")
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
    crash_dumped = False
    initial_log_tail = read_log_tail(LOG_FILE)
    kernel_started = any(marker in initial_log_tail for marker in KERNEL_LOG_MARKERS)
    post_boot_marker_seen = SUCCESS_LOG_MARKER in initial_log_tail
    post_boot_launch_at = None
    post_boot_capture_at = None
    post_boot_finish_at = None
    smp_tables = SmpStatTablePrinter()
    taskmgr_cadence = TaskMgrCadencePrinter()

    def feed_runtime_line(line):
        if ENABLE_SMP_TABLE:
            smp_tables.feed(line)
        taskmgr_cadence.feed(line)

    log_follower = SerialLogFollower(LOG_FILE, feed_runtime_line)
    log_follower.read_available()

    try:
        if post_boot_marker_seen:
            print(f"Runtime matrix completed: {SUCCESS_LOG_MARKER}")
            if not POST_BOOT_PROGRAM:
                force_kill_vm()
                return
            post_boot_launch_at = time.time() + POST_BOOT_DELAY
            print(f"Post-boot launch scheduled in {POST_BOOT_DELAY:g} seconds.")

        while True:
            time.sleep(0.5)

            # 1. Check Hard Timeout
            total_runtime = time.time() - overall_start_time
            if total_runtime > HARD_TIMEOUT:
                print(f"HARD TIMEOUT REACHED! Running for {total_runtime:.1f} seconds.")
                capture_gdb_dump("hard timeout")
                # Try to translate any backtrace before exiting
                check_and_translate_backtrace(LOG_FILE)
                force_kill_vm()
                return

            # 2. Check Process Status (QEMU only)
            if use_qemu and qemu_process and qemu_process.poll() is not None:
                print(f"QEMU exited with code {qemu_process.returncode}")
                # Try to translate any backtrace before exiting
                check_and_translate_backtrace(LOG_FILE)
                return

            # 3. Check Log Stall
            current_size = get_file_size(LOG_FILE)
            current_time = time.time()
            log_changed = current_size != last_size

            if log_changed:
                last_size = current_size
                last_change_time = current_time
                log_follower.read_available()
                log_tail = read_log_tail(LOG_FILE)
                if not kernel_started:
                    kernel_started = any(marker in log_tail for marker in KERNEL_LOG_MARKERS)
                if not post_boot_marker_seen and SUCCESS_LOG_MARKER in log_tail:
                    post_boot_marker_seen = True
                    print(f"Runtime matrix completed: {SUCCESS_LOG_MARKER}")
                    if not POST_BOOT_PROGRAM:
                        force_kill_vm()
                        return
                    post_boot_launch_at = current_time + POST_BOOT_DELAY
                    print(f"Post-boot launch scheduled in {POST_BOOT_DELAY:g} seconds.")
                if not crash_dumped and log_has_crash_marker(log_tail):
                    crash_dumped = True
                    print("Crash/debugger marker detected in serial log.")
                    capture_gdb_dump("serial crash marker")
                    check_and_translate_backtrace(LOG_FILE)
                    force_kill_vm()
                    return

            if post_boot_launch_at is not None and current_time >= post_boot_launch_at:
                print(f"Launching post-boot program: {POST_BOOT_PROGRAM}")
                try:
                    launch_post_boot_program()
                except (OSError, RuntimeError, ValueError) as error:
                    print(f"Post-boot launch failed: {error}")
                    force_kill_vm()
                    return
                post_boot_launch_at = None
                post_boot_capture_at = time.time() + POST_BOOT_CAPTURE_DELAY
                last_change_time = time.time()
                print(f"Screenshot scheduled in {POST_BOOT_CAPTURE_DELAY:g} seconds.")

            if post_boot_capture_at is not None and current_time >= post_boot_capture_at:
                try:
                    captured = capture_post_boot_screenshot()
                except (OSError, RuntimeError) as error:
                    print(f"Post-boot screenshot failed: {error}")
                    force_kill_vm()
                    return
                if not captured:
                    print(f"Post-boot screenshot was not created: {POST_BOOT_SCREENSHOT}")
                    force_kill_vm()
                    return
                print(f"Post-boot screenshot: {POST_BOOT_SCREENSHOT}")
                post_boot_capture_at = None
                post_boot_finish_at = time.time() + 2
                last_change_time = time.time()

            if post_boot_finish_at is not None and current_time >= post_boot_finish_at:
                log_follower.read_available()
                print("Post-boot program capture completed.")
                force_kill_vm()
                return

            post_boot_pending = POST_BOOT_PROGRAM and post_boot_marker_seen
            if not log_changed and not post_boot_pending:
                stall_duration = current_time - last_change_time

                if kernel_started and stall_duration >= STALL_TIMEOUT:
                    print(f"STALL DETECTED! Log unchanged for {stall_duration:.1f} seconds")

                    capture_gdb_dump("serial stall")

                    # Try to translate any backtrace before logging stall
                    check_and_translate_backtrace(LOG_FILE)

                    has_xhci, has_usb = check_log_contents(LOG_FILE)
                    stall_msg = f"Stall detected after {stall_duration:.1f}s. XHCI={has_xhci}, USB={has_usb}"
                    append_to_log(LOG_FILE, stall_msg)

                    force_kill_vm()
                    return

    except KeyboardInterrupt:
        print("\nMonitoring interrupted.")
    finally:
        log_follower.finish()
        if ENABLE_SMP_TABLE:
            smp_tables.finish()
        taskmgr_cadence.finish()


def signal_handler(sig, frame):
    force_kill_vm()
    sys.exit(0)


def main():
    global use_qemu, target_arch, boot_media, boot_image_path, QEMU_ARM64_GIC_VERSION, POST_BOOT_PROGRAM

    parser = argparse.ArgumentParser(description='VM Monitor Script')
    parser.add_argument('--qemu', action='store_true',
                        help='Use QEMU instead of VirtualBox; by default builds and boots livecd.iso')
    parser.add_argument('--vbox', action='store_true', help='Use VirtualBox (default behavior)')
    parser.add_argument('--rpi', action='store_true', help='Use Raspberry Pi emulation mode (cortex-a72, no HVF)')
    parser.add_argument('--smp', type=int, default=4, help='Number of CPU cores (default: 4)')
    parser.add_argument('--img', action='store_true', help='Build and boot ReactOS.img instead of the default QEMU livecd.iso')
    parser.add_argument('--livecd', action='store_true', help='Build and boot livecd.iso instead of ReactOS.img')
    parser.add_argument('--iso', nargs='?', const=LIVECD_ISO, default=None,
                        help='Boot an ISO path with QEMU; no path means build/livecd.iso')
    parser.add_argument('--run-winver', action='store_true',
                        help='On AMD64, launch the SysWOW64 winver.exe after BOOT_TESTS_DONE and capture the screen')
    parser.add_argument('--gic-version', '--gic', choices=('auto', '2', '3', '4', 'host', 'max'),
                        default=QEMU_ARM64_GIC_VERSION,
                        help='ARM64 QEMU virt GIC version (default: auto; env: ROS_QEMU_GIC_VERSION)')
    args = parser.parse_args()
    QEMU_ARM64_GIC_VERSION = args.gic_version

    if args.run_winver and args.vbox:
        parser.error("--run-winver requires QEMU")
    if args.run_winver and args.img:
        parser.error("--run-winver boots the LiveCD and cannot be combined with --img")

    # --vbox is explicit but same as default (no --qemu)
    use_qemu = (args.qemu or args.run_winver) and not args.vbox

    if args.img and (args.livecd or args.iso is not None):
        parser.error("--img cannot be combined with --livecd or --iso")

    # Detect target architecture from CWD
    target_arch = detect_target_arch()
    if args.run_winver:
        if target_arch != "amd64":
            parser.error("--run-winver currently requires an AMD64 build with WoW64 enabled")
        POST_BOOT_PROGRAM = r"X:\reactos\SysWOW64\winver.exe"

    build_target = "reactosimg"
    boot_media = "disk"
    boot_image_path = REACTOS_IMG

    if args.img:
        build_target = "reactosimg"
        boot_media = "disk"
        boot_image_path = REACTOS_IMG
    elif args.livecd or args.iso is not None or use_qemu:
        boot_media = "iso"
        boot_image_path = os.path.realpath(args.iso if args.iso is not None else LIVECD_ISO)
        use_qemu = True
        if boot_image_path == LIVECD_ISO:
            build_target = "livecd"
        else:
            build_target = None
    elif use_qemu:
        boot_media = "iso"
        boot_image_path = LIVECD_ISO
        build_target = "livecd"
    
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
    print(f"Boot media: {boot_media}")
    print(f"Boot image: {boot_image_path}")
    if POST_BOOT_PROGRAM:
        print(f"Post-boot program: {POST_BOOT_PROGRAM}")
        print(f"Post-boot screenshot: {POST_BOOT_SCREENSHOT}")
    print("="*60 + "\n")

    if build_target:
        if not build_ninja_target(build_target):
            sys.exit(1)
    elif not os.path.exists(boot_image_path):
        print(f"Error: boot image not found: {boot_image_path}")
        sys.exit(1)

    # Cleanup: Stop only previous QEMU instances using this build's OS image.
    is_darwin = platform.system() == "Darwin"

    if use_qemu and not is_darwin:
        kill_qemu_for_image(boot_image_path, "before start")

    if use_qemu and target_arch != "arm64" and boot_media != "iso":
        if not create_fat32_img():
            print("Warning: Could not create FAT32 image...")

    if not start_vm(rpi_mode=args.rpi, smp=args.smp):
        sys.exit(1)

    time.sleep(2)

    monitor_log()

    force_kill_vm()


if __name__ == "__main__":
    main()
