#!/usr/bin/env python3
"""
VM Monitor Script
Builds livecd, starts VM (VirtualBox or QEMU) and monitors for stalls.
If log stops updating for more than 10 seconds, forcefully stops the VM.

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

LOG_FILE = "/tmp/v.log"
STALL_TIMEOUT = 10  # seconds
# Use environment variable or default to AMD64
BUILD_DIR = os.environ.get("REACTOS_BUILD_DIR", os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/output-MinGW-amd64-Debug")
FAT32_IMG = os.path.join(BUILD_DIR, "fat32.img")
VM_NAME = "ROSAHCI1"

# Global state
qemu_process = None
use_qemu = False


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
            subprocess.run(["pkill", "-9", "-f", "qemu-system-x86_64.*livecd.iso"],
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
    global qemu_process

    livecd_path = os.path.join(BUILD_DIR, "livecd.iso")

    print(f"Starting QEMU with xHCI USB...")
    print(f"  LiveCD: {livecd_path}")
    print(f"  FAT32 USB disk: {FAT32_IMG}")
    print(f"  Serial output: {LOG_FILE}")

    try:
        open(LOG_FILE, 'w').close()
    except Exception:
        pass

    try:
        log_fd = open(LOG_FILE, 'w')

        qemu_cmd = [
            "qemu-system-x86_64",
            "-enable-kvm",
            "-smp", "1",
            "-m", "3G",
            "-M", "q35",
            "-drive", f"file={livecd_path}",
            "-device", "qemu-xhci,id=xhci",
            "-drive", f"if=none,id=usbdisk,file={FAT32_IMG}",
            "-device", "usb-storage,drive=usbdisk",
            "-serial", "stdio",
            "-device", "usb-kbd",
            "-device", "usb-tablet",
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
        print("Error: qemu-system-x86_64 not found. Is QEMU installed?")
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
    """Monitor log file for stalls."""
    global qemu_process, use_qemu

    stall_count = 0
    last_size = -1
    last_change_time = time.time()

    print(f"Monitoring log file: {LOG_FILE}")
    print(f"Stall timeout: {STALL_TIMEOUT} seconds")
    print("Press Ctrl+C to stop monitoring.\n")

    # Wait for log file to appear and have content
    wait_count = 0
    while get_file_size(LOG_FILE) <= 0:
        if wait_count % 10 == 0:
            print(f"Waiting for log output...")
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

            if use_qemu and qemu_process and qemu_process.poll() is not None:
                print(f"\nQEMU exited with code {qemu_process.returncode}")
                print(f"Log available at: {LOG_FILE}")
                return

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
                    print(f"Stall count: {stall_count}")
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

    atexit.register(force_kill_vm)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGHUP, signal_handler)
    signal.signal(signal.SIGQUIT, signal_handler)

    vm_type = "QEMU with xHCI USB" if use_qemu else "VirtualBox"
    print("="*60)
    print(f"VM Monitor Script ({vm_type})")
    print("="*60 + "\n")

    if not build_livecd():
        print("Aborting due to build failure.")
        sys.exit(1)

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
