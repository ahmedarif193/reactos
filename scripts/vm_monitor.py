#!/usr/bin/env python3
"""
VM Monitor Script
Builds reactosimg, starts VM (VirtualBox or QEMU) and monitors for stalls.
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
import re
import bisect
import glob

# Configuration (never change those values)
LOG_FILE = "/tmp/freeldr_arm64.log"
STALL_TIMEOUT = int(os.environ.get("ROS_VM_STALL_TIMEOUT", "10"))
HARD_TIMEOUT = int(os.environ.get("ROS_VM_HARD_TIMEOUT", "30"))
VM_NAME = os.environ.get("ROS_VM_NAME", "ROS11")

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
    for arg in cmdline[1:]:
        if arg == image_path or resolve_process_path(arg, process_cwd) == image_real:
            return True

        for item in arg.split(","):
            if item.startswith("file="):
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


def kill_qemu_for_image(image_path, reason):
    """Kill only QEMU processes that are using the ReactOS image for this build."""
    matches = find_qemu_pids_for_image(image_path)
    if not matches:
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


def force_kill_vm():
    """Forcefully kill VM - called on exit."""
    global qemu_process, use_qemu

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
        kill_qemu_for_image(REACTOS_IMG, "monitor cleanup")
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


def build_reactosimg():
    """Build reactosimg using ninja before starting VM."""
    global target_arch

    # Detect architecture first if not set
    if target_arch == "amd64":
        build_dir_lower = BUILD_DIR.lower()
        if "arm64" in build_dir_lower or "aarch64" in build_dir_lower:
            target_arch = "arm64"

    print(f"Building reactosimg in {BUILD_DIR}...")
    try:
        result = subprocess.run(
            ["ninja", "reactosimg"],
            cwd=BUILD_DIR,
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
        print(f"Error building reactosimg: {e}")
        return False


def start_qemu(rpi_mode=False, smp=4):
    """Start QEMU based on architecture."""
    global qemu_process, target_arch

    img_path = REACTOS_IMG

    # Reset log file
    try:
        open(LOG_FILE, 'w').close()
    except Exception as e:
        print(f"Error resetting log file: {e}")
        return False

    # ---------------- ARM64 CONFIGURATION ----------------
    if target_arch == "arm64":
        is_darwin = platform.system() == "Darwin"
        if rpi_mode:
            mode_str = f"RPI emulation (cortex-a72, {smp} cores)"
        else:
            mode_str = f"HVF accelerated (max, {smp} cores)" if is_darwin else f"CPU max ({smp} cores)"
        print(f"Starting QEMU (ARM64 - {mode_str})...")

        # Darwin-specific configuration (macOS)
        if is_darwin:
            if rpi_mode:
                # Raspberry Pi emulation mode (cortex-a72, no HVF)
                # Use -serial stdio with stdout redirect instead of -serial file:
                # because -serial file: has buffering issues on macOS QEMU
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", str(smp),
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "cortex-a72",
                    "-m", "4G",
                    "-drive", "if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-aarch64-code.fd",
                    "-drive", f"file={img_path}",
                    "-boot", "order=d,menu=on",
                    "-display", "none",
                    "-serial", "stdio"
                ]
            else:
                # HVF accelerated mode (max CPU features)
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-accel", "hvf",
                    "-smp", str(smp),
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "max",
                    "-m", "4G",
                    "-drive", "if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-aarch64-code.fd",
                    "-drive", f"file={img_path}",
                    "-boot", "order=d,menu=on",
                    "-display", "none",
                    "-serial", f"file:{LOG_FILE}"
                ]
        else:
            # Linux/other systems
            # Dual-attach strategy:
            #   1. VirtIO drive  → UEFI can enumerate and boot from it
            #   2. AHCI + ide-cd → storahci.sys is a BOOT driver; it creates
            #      \Device\CdRom0 during IopInitializeBootDrivers, so
            #      IopCreateArcNames can map the ARC name and \SystemRoot
            #      is resolved before system drivers try to load.
            # Without the virtio drive UEFI has nothing to boot from.
            # Without the AHCI drive ReactOS has no boot-class CD driver →
            # deadlock (virtio-blk.sys is SYSTEM-start, not BOOT-start).
            if rpi_mode:
                # Raspberry Pi emulation mode (cortex-a72, no KVM)
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", str(smp),
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "cortex-a72",
                    "-m", "4G",
                    "-bios", "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
                    "-drive", f"file={img_path}",
                    "-display", "none",
                    "-serial", "stdio"
                ]
            else:
                # Default accelerated mode
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-smp", str(smp),
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "max",
                    "-m", "4G",
                    "-bios", "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd",
                    "-drive", f"file={img_path}",
                    "-display", "none",
                    "-serial", f"file:{LOG_FILE}"
                ]

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
    print(f"  Disk image: {img_path}")
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
                    "-drive", f"file={img_path}",
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
                    "-smp", "1",
                    "-m", "3G",
                    "-M", "q35",
                    "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
                    "-drive", f"file={img_path}",
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
                "-m", "3G",
                "-drive", f"file={img_path}",
                "-serial", f"file:{LOG_FILE}",
                "-device", "qemu-xhci,id=xhci",
                "-device", "usb-kbd,bus=xhci.0",
                "-device", "usb-mouse,bus=xhci.0",
                "-drive", f"if=none,id=usbdisk,format=raw,file={FAT32_IMG}",
                "-device", "usb-storage,bus=xhci.0,drive=usbdisk"
            ]

        # Add acceleration: TCG on macOS, KVM on Linux
        if is_darwin:
            qemu_cmd.insert(1, "-accel")
            qemu_cmd.insert(2, "tcg")
        else:
            qemu_cmd.insert(1, "-enable-kvm")

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


# Global symbol cache: module_name -> list of (rva, name) sorted by rva
_symbol_cache = {}
_image_base_cache = {}
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
        # Try llvm-addr2line first (better for ARM64)
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

    # Try regular addr2line as fallback
    try:
        addr2line = shutil.which('addr2line')
        if not addr2line:
            raise FileNotFoundError("addr2line not found")
        result = subprocess.run(
            [addr2line, '-e', binary_path, '-f', '-C', hex(offset)],
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

            if current_size != last_size:
                last_size = current_size
                last_change_time = current_time
            else:
                stall_duration = current_time - last_change_time

                if stall_duration >= STALL_TIMEOUT:
                    print(f"STALL DETECTED! Log unchanged for {stall_duration:.1f} seconds")

                    # Try to translate any backtrace before logging stall
                    check_and_translate_backtrace(LOG_FILE)

                    has_xhci, has_usb = check_log_contents(LOG_FILE)
                    stall_msg = f"Stall detected after {stall_duration:.1f}s. XHCI={has_xhci}, USB={has_usb}"
                    append_to_log(LOG_FILE, stall_msg)

                    force_kill_vm()
                    return

    except KeyboardInterrupt:
        print("\nMonitoring interrupted.")


def signal_handler(sig, frame):
    force_kill_vm()
    sys.exit(0)


def main():
    global use_qemu, target_arch

    parser = argparse.ArgumentParser(description='VM Monitor Script')
    parser.add_argument('--qemu', action='store_true', help='Use QEMU instead of VirtualBox')
    parser.add_argument('--vbox', action='store_true', help='Use VirtualBox (default behavior)')
    parser.add_argument('--rpi', action='store_true', help='Use Raspberry Pi emulation mode (cortex-a72, no HVF)')
    parser.add_argument('--smp', type=int, default=4, help='Number of CPU cores (default: 4)')
    args = parser.parse_args()

    # --vbox is explicit but same as default (no --qemu)
    use_qemu = args.qemu and not args.vbox

    # Detect target architecture from CWD
    target_arch = detect_target_arch()
    
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

    if not build_reactosimg():
        sys.exit(1)

    # Cleanup: Stop only previous QEMU instances using this build's OS image.
    is_darwin = platform.system() == "Darwin"

    if use_qemu and not is_darwin:
        kill_qemu_for_image(REACTOS_IMG, "before start")

    if use_qemu and target_arch != "arm64":
        if not create_fat32_img():
            print("Warning: Could not create FAT32 image...")

    if not start_vm(rpi_mode=args.rpi, smp=args.smp):
        sys.exit(1)

    time.sleep(2)

    monitor_log()

    force_kill_vm()


if __name__ == "__main__":
    main()
