#!/usr/bin/env python3
"""
VM Monitor Script
Builds livecd, starts VM (VirtualBox or QEMU) and monitors for stalls.
If log stops updating for more than 6 seconds, or total runtime exceeds 30 seconds,
forcefully stops the VM.

Usage:
  python3 vm_monitor.py          # Use VirtualBox (default)
  python3 vm_monitor.py --qemu   # Use QEMU with xHCI USB
"""

import subprocess
import time
import os
import sys
import signal
import atexit
import argparse
import shutil

LOG_FILE = "/tmp/v.log"
STALL_TIMEOUT = 6    # Log inactivity timeout
HARD_TIMEOUT = 30    # Total maximum runtime seconds

# Use environment variable, or current directory if it looks like a build dir, otherwise default to AMD64
def get_build_dir():
    if "REACTOS_BUILD_DIR" in os.environ:
        return os.environ["REACTOS_BUILD_DIR"]
    cwd = os.getcwd()
    # Check if current directory looks like a ReactOS build directory
    if os.path.exists(os.path.join(cwd, "build.ninja")) or "output-" in cwd:
        return cwd
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/output-MinGW-amd64-Debug"
BUILD_DIR = get_build_dir()
FAT32_IMG = os.path.join(BUILD_DIR, "fat32.img")
VM_NAME = "ROSAHCI1"

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
target_arch = "amd64"  # Detected from BUILD_DIR


def detect_target_arch():
    """Detect target architecture from build directory name."""
    global target_arch
    build_dir_lower = BUILD_DIR.lower()
    if "i386" in build_dir_lower or "x86" in build_dir_lower:
        target_arch = "i386"
    elif "amd64" in build_dir_lower or "x64" in build_dir_lower:
        target_arch = "amd64"
    else:
        # Default to amd64 if not detected
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
        print("FAT32 image created successfully.")
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
                print("QEMU terminated.")
            except Exception:
                try:
                    qemu_process.kill()
                    print("QEMU killed forcefully.")
                except Exception:
                    pass
        try:
            # Kill both possible QEMU binaries
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
            print(f"VM '{VM_NAME}' forcefully killed.")
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
            print(result.stdout.decode('utf-8', errors='ignore'))
            return False
        print("Build completed successfully.")
        return True
    except subprocess.TimeoutExpired:
        print("Error: Build timed out after 10 minutes")
        return False
    except FileNotFoundError:
        print("Error: ninja not found. Is it installed?")
        return False
    except Exception as e:
        print(f"Error building livecd: {e}")
        return False


def start_qemu():
    """Start QEMU with xHCI USB controller."""
    global qemu_process, target_arch

    livecd_path = os.path.join(BUILD_DIR, "livecd.iso")

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
        open(LOG_FILE, 'w').close()
    except Exception:
        pass

    try:
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
            "-no-shutdown"  # Halt instead of reset on triple fault
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

    except FileNotFoundError:
        print(f"Error: {qemu_binary} not found. Is QEMU installed?")
        return False
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
    except FileNotFoundError:
        print("Error: VBoxManage not found. Is VirtualBox installed?")
        return False
    except Exception as e:
        print(f"Error starting VM: {e}")
        return False


def start_vm():
    """Start the VM (QEMU or VirtualBox)."""
    if use_qemu:
        return start_qemu()
    else:
        return start_vbox()


def get_file_size(filepath):
    """Get file size, returns -1 if file doesn't exist."""
    try:
        return os.path.getsize(filepath)
    except OSError:
        return -1


def check_log_contents(filepath):
    """Check log file for specific strings."""
    try:
        with open(filepath, 'r', errors='ignore') as f:
            content = f.read()
        has_xhci = "usbxhci" in content.lower() or "xhci" in content.lower()
        has_usb = "usb" in content.lower() and "device" in content.lower()
        return has_xhci, has_usb
    except Exception:
        return False, False


def append_to_log(filepath, message):
    """Append a message to the log file."""
    try:
        with open(filepath, 'a') as f:
            f.write(f"\n{'='*60}\n")
            f.write(f"{message}\n")
            f.write(f"{'='*60}\n")
        return True
    except Exception as e:
        print(f"Error appending to log: {e}")
        return False


def monitor_log():
    """Monitor log file for stalls and enforce hard timeout."""
    global qemu_process, use_qemu

    stall_count = 0
    last_size = -1
    last_change_time = time.time()
    
    # Track overall runtime for Hard Timeout
    overall_start_time = time.time()

    print(f"Monitoring log file: {LOG_FILE}")
    print(f"Stall timeout: {STALL_TIMEOUT} seconds")
    print(f"Hard timeout: {HARD_TIMEOUT} seconds")
    print("Press Ctrl+C to stop monitoring.\n")

    # Wait for log file to appear and have content
    wait_count = 0
    while get_file_size(LOG_FILE) <= 0:
        # Check hard timeout while waiting for log
        if time.time() - overall_start_time > HARD_TIMEOUT:
            print(f"\n{'='*60}")
            print(f"HARD TIMEOUT ({HARD_TIMEOUT}s) reached while waiting for log creation.")
            print(f"{'='*60}")
            force_kill_vm()
            return

        if wait_count % 10 == 0:
            print(f"Waiting for log output... ({int(time.time() - overall_start_time)}s elapsed)")
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
                print(f"\n{'='*60}")
                print(f"HARD TIMEOUT REACHED! Running for {total_runtime:.1f} seconds.")
                print(f"Stopping VM regardless of activity.")
                print(f"{'='*60}")
                force_kill_vm()
                return

            # 2. Check Process Status (QEMU only)
            if use_qemu and qemu_process and qemu_process.poll() is not None:
                print(f"\nQEMU exited with code {qemu_process.returncode}")
                print(f"Log available at: {LOG_FILE}")
                return

            # 3. Check Log Stall
            current_size = get_file_size(LOG_FILE)
            current_time = time.time()

            if current_size != last_size:
                last_size = current_size
                last_change_time = current_time
                stall_count = 0
            else:
                stall_duration = current_time - last_change_time

                if stall_duration >= STALL_TIMEOUT:
                    stall_count += 1
                    print(f"\n{'='*60}")
                    print(f"STALL DETECTED! Log unchanged for {stall_duration:.1f} seconds")
                    print(f"{'='*60}")

                    has_xhci, has_usb = check_log_contents(LOG_FILE)
                    if has_xhci:
                        print("XHCI driver activity detected in log.")
                    if has_usb:
                        print("USB device activity detected in log.")

                    stall_msg = f"Stall detected after {stall_duration:.1f}s. XHCI={has_xhci}, USB={has_usb}"
                    append_to_log(LOG_FILE, stall_msg)

                    force_kill_vm()

                    print(f"\n{'='*60}")
                    print(f"VM exited.")
                    print(f"Log available at: {LOG_FILE}")
                    print(f"{'='*60}")
                    return

    except KeyboardInterrupt:
        print("\n\nMonitoring interrupted by user.")
        print(f"Log available at: {LOG_FILE}")


def signal_handler(sig, frame):
    """Handle interrupt signals - force kill VM before exit."""
    print(f"\n\nReceived signal {sig}. Killing VM and exiting...")
    force_kill_vm()
    sys.exit(0)


def main():
    global use_qemu

    parser = argparse.ArgumentParser(description='VM Monitor Script')
    parser.add_argument('--qemu', action='store_true', help='Use QEMU instead of VirtualBox')
    args = parser.parse_args()

    use_qemu = args.qemu

    # Detect target architecture from build directory
    arch = detect_target_arch()

    atexit.register(force_kill_vm)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGHUP, signal_handler)
    signal.signal(signal.SIGQUIT, signal_handler)

    vm_type = f"QEMU ({arch}) with xHCI USB" if use_qemu else "VirtualBox"
    print("="*60)
    print(f"VM Monitor Script ({vm_type})")
    print(f"Build directory: {BUILD_DIR}")
    print("="*60 + "\n")

    if not build_livecd():
        print("Aborting due to build failure.")
        sys.exit(1)

    # Clean up previous QEMU instances before starting a new one
    print("Ensuring previous QEMU instances are stopped...")
    try:
        subprocess.run("sudo kill -9 $(pidof qemu-system-x86_64)", shell=True, 
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass

    if use_qemu:
        if not create_fat32_img():
            print("Warning: Could not create FAT32 image, continuing anyway...")

    if not start_vm():
        sys.exit(1)

    time.sleep(2)

    monitor_log()

    force_kill_vm()


if __name__ == "__main__":
    main()
