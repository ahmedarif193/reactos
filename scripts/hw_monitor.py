#!/usr/bin/env python3
"""
Hardware Monitor Script
Builds livecd, deploys ISO to HTTP server, sends reset command over serial,
and monitors the serial log for stalls.

The target board (e.g. LattePanda Mu) boots via UEFI HTTP boot and downloads
the ISO from http://<host>/livecd.iso.

Usage:
  # Run from within your build directory (e.g., output-MinGW-amd64-Debug)
  python3 ../scripts/hw_monitor.py

  # Specify serial port explicitly:
  python3 ../scripts/hw_monitor.py --port /dev/ttyUSB0

  # Skip build:
  python3 ../scripts/hw_monitor.py --no-build
"""

import subprocess
import time
import os
import sys
import signal
import atexit
import argparse
import shutil
import serial  # pyserial

# Configuration
LOG_FILE = "/tmp/hw.log"
ISO_DEPLOY_PATH = "/var/www/html/livecd.iso"
STALL_TIMEOUT = 16       # Log inactivity timeout (seconds) — DO NOT CHANGE
HARD_TIMEOUT = 5 * 60    # Total maximum runtime (5 minutes)
SERIAL_BAUD = 115200
RESET_CHAR = "§"         # Character sent over serial to trigger board reset

# Default serial port candidates (Linux)
SERIAL_PORT_CANDIDATES = [
    "/dev/ttyUSB0",
    "/dev/ttyUSB1",
    "/dev/ttyACM0",
    "/dev/ttyACM1",
    "/dev/ttyS0",
]

# Global state
serial_port = None


def get_build_dir():
    """
    Returns the current working directory as the build directory.
    Strictly assumes the script is executed FROM the output directory.
    """
    if "REACTOS_BUILD_DIR" in os.environ:
        return os.environ["REACTOS_BUILD_DIR"]

    cwd = os.getcwd()

    if "output-" in os.path.basename(cwd) or os.path.exists(os.path.join(cwd, "build.ninja")):
        return cwd

    print(f"Warning: Current directory '{cwd}' does not look like a standard 'output-' directory.")
    print("Proceeding using current directory as BUILD_DIR...")
    return cwd


BUILD_DIR = get_build_dir()


def find_serial_port():
    """Find the first available serial port."""
    for port in SERIAL_PORT_CANDIDATES:
        if os.path.exists(port):
            return port
    return None


def cleanup():
    """Close serial port on exit."""
    global serial_port
    if serial_port and serial_port.is_open:
        try:
            serial_port.close()
        except Exception:
            pass


def build_livecd():
    """Build livecd using ninja."""
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
            output = result.stdout.decode(errors='ignore')
            # Show last 20 lines of build output on failure
            lines = output.strip().split('\n')
            for line in lines[-20:]:
                print(f"  {line}")
            return False
        print("Build completed successfully.")
        return True
    except Exception as e:
        print(f"Error building livecd: {e}")
        return False


def deploy_iso():
    """Copy livecd.iso to the HTTP server directory."""
    src = os.path.join(BUILD_DIR, "livecd.iso")
    dst = ISO_DEPLOY_PATH

    if not os.path.exists(src):
        print(f"Error: livecd.iso not found at {src}")
        return False

    print(f"Deploying ISO: {src} -> {dst}")
    try:
        shutil.copy2(src, dst)
        # Verify copy
        src_size = os.path.getsize(src)
        dst_size = os.path.getsize(dst)
        if src_size != dst_size:
            print(f"Error: Size mismatch after copy (src={src_size}, dst={dst_size})")
            return False
        print(f"ISO deployed ({src_size / (1024*1024):.0f} MiB)")
        return True
    except PermissionError:
        print(f"Permission denied copying to {dst}. Trying with sudo...")
        try:
            result = subprocess.run(
                ["sudo", "cp", src, dst],
                timeout=30,
                capture_output=True
            )
            if result.returncode == 0:
                print(f"ISO deployed with sudo ({os.path.getsize(src) / (1024*1024):.0f} MiB)")
                return True
            else:
                print(f"sudo cp failed: {result.stderr.decode(errors='ignore')}")
                return False
        except Exception as e:
            print(f"Error deploying ISO with sudo: {e}")
            return False
    except Exception as e:
        print(f"Error deploying ISO: {e}")
        return False


def kill_port_holder(port_path):
    """Find and kill the process holding a serial port."""
    try:
        result = subprocess.run(
            ["sudo", "fuser", port_path],
            capture_output=True, timeout=5
        )
        pids = result.stdout.decode(errors='ignore').split()
        if not pids:
            # fuser sometimes outputs to stderr
            pids = result.stderr.decode(errors='ignore').split()
        # Filter to numeric PIDs only
        pids = [p.strip().rstrip('m') for p in pids if p.strip().rstrip('m').isdigit()]
        if not pids:
            return False
        for pid in pids:
            print(f"Killing PID {pid} holding {port_path}...")
            subprocess.run(["sudo", "kill", "-9", pid], timeout=5)
        time.sleep(0.5)
        return True
    except Exception as e:
        print(f"Failed to kill port holder: {e}")
        return False


def open_serial(port_path):
    """Open the serial port, unconditionally killing any process that holds it first."""
    global serial_port

    # Always kill whatever is using the port before we try to open it
    kill_port_holder(port_path)

    try:
        serial_port = serial.Serial(
            port=port_path,
            baudrate=SERIAL_BAUD,
            timeout=0.5,  # read timeout
            write_timeout=2,
        )
        print(f"Serial port opened: {port_path} @ {SERIAL_BAUD} baud")
        return True
    except Exception as e:
        print(f"Error opening serial port {port_path}: {e}")
        return False


def send_reset():
    """Send reset character over serial."""
    global serial_port
    if not serial_port or not serial_port.is_open:
        print("Error: Serial port not open")
        return False

    try:
        serial_port.write(RESET_CHAR.encode('utf-8'))
        serial_port.flush()
        print(f"Reset command sent ('{RESET_CHAR}')")
        return True
    except Exception as e:
        print(f"Error sending reset: {e}")
        return False


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


def archive_log():
    """Copy latest log to a timestamped file before exit."""
    try:
        if not os.path.exists(LOG_FILE):
            return False
        timestamp = time.strftime("%Y%m%d-%H%M%S")
        dst = f"/tmp/hw-timestamp-{timestamp}.log"
        shutil.copy2(LOG_FILE, dst)
        print(f"Archived log: {dst}")
        return True
    except Exception as e:
        print(f"Failed to archive log: {e}")
        return False


def monitor_serial():
    """Read serial output, write to log file, and detect stalls."""
    global serial_port

    # Reset log file
    try:
        open(LOG_FILE, 'w').close()
    except Exception as e:
        print(f"Error resetting log file: {e}")
        return

    last_change_time = time.time()
    overall_start_time = time.time()

    print(f"Monitoring serial output -> {LOG_FILE}")
    print(f"Stall timeout: {STALL_TIMEOUT}s | Hard timeout: {HARD_TIMEOUT}s")
    print("-" * 60)

    try:
        with open(LOG_FILE, 'ab') as log_fd:
            while True:
                # 1. Check Hard Timeout
                total_runtime = time.time() - overall_start_time
                if total_runtime > HARD_TIMEOUT:
                    msg = f"HARD TIMEOUT REACHED ({HARD_TIMEOUT}s)"
                    print(f"\n{msg}")
                    append_to_log(LOG_FILE, msg)
                    return

                # 2. Read serial data
                try:
                    data = serial_port.read(4096)
                except Exception as e:
                    print(f"\nSerial read error: {e}")
                    return

                if data:
                    log_fd.write(data)
                    log_fd.flush()
                    last_change_time = time.time()
                else:
                    # No data received, check for stall
                    stall_duration = time.time() - last_change_time
                    if stall_duration >= STALL_TIMEOUT:
                        msg = (f"HW stopped logging, end of boot process "
                               f"(stall of {stall_duration:.1f}s, do not change stall timer). "
                               f"Log available at {LOG_FILE}")
                        print(f"\n{msg}")
                        append_to_log(LOG_FILE, msg)
                        return

    except KeyboardInterrupt:
        print("\nMonitoring interrupted.")


def signal_handler(sig, frame):
    cleanup()
    sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description='Hardware Monitor Script')
    parser.add_argument('--port', type=str, default=None,
                        help='Serial port (e.g. /dev/ttyUSB0)')
    parser.add_argument('--no-build', action='store_true',
                        help='Skip building livecd')
    parser.add_argument('--no-deploy', action='store_true',
                        help='Skip deploying ISO to HTTP server')
    parser.add_argument('--no-reset', action='store_true',
                        help='Skip sending reset command')
    args = parser.parse_args()

    atexit.register(cleanup)
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    print("=" * 60)
    print("Hardware Monitor Script")
    print(f"Build directory: {BUILD_DIR}")
    print(f"ISO deploy path: {ISO_DEPLOY_PATH}")
    print(f"Log file:        {LOG_FILE}")
    print("=" * 60 + "\n")

    # Step 1: Build
    if not args.no_build:
        if not build_livecd():
            sys.exit(1)
    else:
        print("Skipping build (--no-build)")

    # Step 2: Deploy ISO
    if not args.no_deploy:
        if not deploy_iso():
            sys.exit(1)
    else:
        print("Skipping ISO deploy (--no-deploy)")

    # Step 3: Open serial port
    port_path = args.port
    if not port_path:
        port_path = find_serial_port()
        if not port_path:
            print("Error: No serial port found. Use --port to specify.")
            sys.exit(1)

    if not open_serial(port_path):
        sys.exit(1)

    # Step 4: Send reset
    if not args.no_reset:
        if not send_reset():
            sys.exit(1)
    else:
        print("Skipping reset (--no-reset)")

    # Step 5: Monitor serial
    print(f"\nWaiting for boot output...\n")
    monitor_serial()
    archive_log()

    cleanup()
    print("\nDone.")


if __name__ == "__main__":
    main()
