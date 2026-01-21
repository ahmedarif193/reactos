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

# Configuration
LOG_FILE = "/tmp/v.log"
STALL_TIMEOUT = 6    # Log inactivity timeout
HARD_TIMEOUT = 30    # Total maximum runtime seconds
VM_NAME = "ROSAHCI1"

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

# UEFI firmware paths - architecture dependent
OVMF_ENV_CODE_VARS = ["REACTOS_OVMF_CODE", "OVMF_CODE"]
OVMF_ENV_VARS_VARS = ["REACTOS_OVMF_VARS", "OVMF_VARS"]

OVMF_X64_CODE_CANDIDATES = [
    "/tmp/OVMF_CODE_latest.fd",
    "/usr/share/OVMF/OVMF_CODE.fd",
    "/usr/share/OVMF/OVMF_CODE_4M.fd",
    "/usr/share/edk2-ovmf/x64/OVMF_CODE.fd",
    "/usr/share/edk2/ovmf/OVMF_CODE.fd",
]

OVMF_X64_VARS_CANDIDATES = [
    "/tmp/OVMF_VARS_latest.fd",
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
        try:
            subprocess.run(["pkill", "-9", "-f", "qemu-system.*livecd.iso"],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        except Exception:
            pass
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


def start_qemu(rpi_mode=False):
    """Start QEMU based on architecture."""
    global qemu_process, target_arch

    livecd_path = os.path.join(BUILD_DIR, "livecd.iso")
    
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
            mode_str = "RPI emulation (cortex-a76)"
        else:
            mode_str = "HVF accelerated (max)" if is_darwin else "CPU max (4 cores)"
        print(f"Starting QEMU (ARM64 - {mode_str})...")

        # Darwin-specific configuration (macOS)
        if is_darwin:
            if rpi_mode:
                # Raspberry Pi emulation mode (cortex-a76, no HVF)
                qemu_cmd = [
                    "qemu-system-aarch64",
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "cortex-a76",
                    "-m", "2G",
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
                    "-smp", "4",
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "max",
                    "-m", "2G",
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
                    "-device", "ramfb",
                    "-machine", "virt,gic-version=3",
                    "-cpu", "cortex-a76",
                    "-m", "2G",
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
                    "-smp", "4",
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
    use_uefi = True
    ovmf_code = None
    ovmf_vars = None

    # Select architecture-specific settings
    qemu_binary = "qemu-system-i386" if target_arch == "i386" else "qemu-system-x86_64"
    ovmf_code, ovmf_vars, ovmf_vars_template, code_candidates, vars_candidates = resolve_ovmf_paths(target_arch)

    print(f"Starting QEMU ({target_arch}) with xHCI USB...")
    print(f"  QEMU binary: {qemu_binary}")
    print(f"  OVMF CODE: {ovmf_code}")
    print(f"  OVMF VARS: {ovmf_vars}")
    print(f"  LiveCD: {livecd_path}")
    print(f"  FAT32 USB disk: {FAT32_IMG}")
    print(f"  Serial output: {LOG_FILE}")

    # Verify OVMF firmware exists
    if not ovmf_code or not os.path.exists(ovmf_code):
        print(f"Error: OVMF CODE not found: {ovmf_code}")
        print("Set REACTOS_OVMF_CODE or OVMF_CODE to override.")
        print(f"Tried: {', '.join(code_candidates)}")
        if target_arch == "i386":
            print("Install ovmf-ia32 package: sudo apt install ovmf-ia32")
        else:
            print("Install ovmf package: sudo apt install ovmf")
        return False
    if not prepare_ovmf_vars(ovmf_vars_template, ovmf_vars):
        return False

    try:
        # Open log file for stdout redirection (x64 approach)
        log_fd = open(LOG_FILE, 'w')

        qemu_cmd = [
            qemu_binary,
            "-enable-kvm",
            "-smp", "1",
            "-m", "3G",
            "-M", "q35",
            "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
            "-cdrom", livecd_path,
            "-boot", "d",  # Boot from CD-ROM
            "-serial", "stdio",
            "-display", "none",
            "-no-reboot",
            "-no-shutdown"
        ]

        qemu_process = subprocess.Popen(
            qemu_cmd,
            cwd=BUILD_DIR,
            stdout=log_fd,
            stderr=subprocess.STDOUT,
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


def start_vm(rpi_mode=False):
    """Start the VM (QEMU or VirtualBox)."""
    if use_qemu:
        return start_qemu(rpi_mode)
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
            else:
                stall_duration = current_time - last_change_time

                if stall_duration >= STALL_TIMEOUT:
                    print(f"STALL DETECTED! Log unchanged for {stall_duration:.1f} seconds")
                    
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
    parser.add_argument('--rpi', action='store_true', help='Use Raspberry Pi emulation mode (cortex-a76, no HVF)')
    args = parser.parse_args()

    use_qemu = args.qemu

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

    if not build_livecd():
        sys.exit(1)

    # Cleanup: Only run on non-ARM64 architectures
    if target_arch != "arm64":
        print("Ensuring previous QEMU instances are stopped...")
        try:
            subprocess.run("sudo kill -9 $(pidof qemu-system-x86_64)", shell=True, 
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run("sudo kill -9 $(pidof qemu-system-i386)", shell=True, 
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

    if not start_vm(rpi_mode=args.rpi):
        sys.exit(1)

    time.sleep(2)

    monitor_log()

    force_kill_vm()


if __name__ == "__main__":
    main()