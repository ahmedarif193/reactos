#!/usr/bin/env python3
"""
Build, flash & boot-test cycle for Raspberry Pi via USB HID relay board.

Run from the build output directory (no sudo needed, elevates internally):
  cd output-Clang-arm64-Debug
  ../scripts/flash_test.py

Flow:
  1. ninja livecd → ninja liveimg  (builds liveimg.img in cwd)
  2. Storage → host  (relay 3,4 ON)
  3. Flash liveimg.img → /dev/sda1
  4. Storage → Pi    (relay 3,4 OFF)
  5. Power Pi        (relay 2 ON)
  6. Monitor serial, detect 6s stall → crash/done
  7. Power off Pi    (relay 2 OFF)
  8. Storage → host  (relay 3,4 ON) — ready for next session
"""

import glob
import os
import subprocess
import sys
import time

import serial

# Build dir = cwd (the output directory you run from)
BUILD_DIR = os.getcwd()
LIVEIMG = os.path.join(BUILD_DIR, "liveimg.img")

# Devices
HIDRAW = "/dev/hidraw1"
FLASH_TARGET = "/dev/sda"
SERIAL_BAUD = 115200

# Debug Probe VID:PID (Raspberry Pi Debug Probe / CMSIS-DAP)
DEBUG_PROBE_VID = "2e8a"
DEBUG_PROBE_PID = "000c"

# Timeouts
STALL_TIMEOUT = 6
MAX_TIMEOUT = 90

# Log
LOG_FILE = os.path.join(BUILD_DIR, "flash_test.log")


def find_serial_port() -> str:
    """Find the tty device for the Debug Probe by VID:PID via sysfs."""
    for dev in glob.glob("/sys/bus/usb/devices/*/"):
        try:
            vid = open(os.path.join(dev, "idVendor")).read().strip()
            pid = open(os.path.join(dev, "idProduct")).read().strip()
        except FileNotFoundError:
            continue
        if vid == DEBUG_PROBE_VID and pid == DEBUG_PROBE_PID:
            # tty is at <device>/<interface>/tty/<ttyACMx>
            for tty in glob.glob(os.path.join(dev, "*/tty/tty*")):
                tty_name = os.path.basename(tty)
                tty_path = f"/dev/{tty_name}"
                if os.path.exists(tty_path):
                    return tty_path
    return None


def sudo_write(path: str, data: bytes):
    """Write raw bytes to a device path via sudo tee."""
    proc = subprocess.run(
        ["sudo", "tee", path],
        input=data, stdout=subprocess.DEVNULL,
    )
    if proc.returncode != 0:
        print(f"[!] Failed to write to {path}", file=sys.stderr)
        sys.exit(1)


def relay(pin: int, state: int):
    header = 0xA0
    checksum = (header + pin + state) & 0xFF
    cmd = bytes([header, pin, state, checksum])
    sudo_write(HIDRAW, cmd)
    label = "ON" if state else "OFF"
    print(f"  relay {pin} → {label}")


def storage_to_host():
    print("[*] Storage → host")
    relay(3, 1)
    relay(4, 1)


def storage_to_pi():
    print("[*] Storage → Pi")
    relay(3, 0)
    relay(4, 0)


def pi_power(state: int):
    label = "ON" if state else "OFF"
    print(f"[*] Pi power → {label}")
    relay(2, state)


def build():
    print(f"[*] Build dir: {BUILD_DIR}")

    # Remove stale image so ninja regenerates with fresh source files
    # (CMake file(GLOB) only runs at configure time, so ninja won't
    #  detect changes to boot media files like config.txt)
    if os.path.exists(LIVEIMG):
        os.remove(LIVEIMG)
        print("[*] Removed stale liveimg.img")

    print("[*] Building livecd...")
    proc = subprocess.run(["ninja", "livecd"], cwd=BUILD_DIR)
    if proc.returncode != 0:
        print("[!] ninja livecd failed", file=sys.stderr)
        sys.exit(1)

    print("[*] Building liveimg...")
    proc = subprocess.run(["ninja", "liveimg"], cwd=BUILD_DIR)
    if proc.returncode != 0:
        print("[!] ninja liveimg failed", file=sys.stderr)
        sys.exit(1)

    if not os.path.exists(LIVEIMG):
        print(f"[!] {LIVEIMG} not found after build", file=sys.stderr)
        sys.exit(1)

    print(f"[*] Build complete: {LIVEIMG}")


def flash():
    # Unmount any partitions on the target device
    subprocess.run(["sudo", "umount", f"{FLASH_TARGET}1"], stderr=subprocess.DEVNULL)
    subprocess.run(["sudo", "umount", FLASH_TARGET], stderr=subprocess.DEVNULL)

    print(f"[*] Flashing {LIVEIMG} → {FLASH_TARGET}")
    proc = subprocess.run(
        ["sudo", "dd", f"if={LIVEIMG}", f"of={FLASH_TARGET}", "bs=4M", "status=progress"],
    )
    if proc.returncode != 0:
        print("[!] Flash failed", file=sys.stderr)
        sys.exit(1)
    subprocess.run(["sudo", "sync"])
    print("[*] Flash complete")


def open_serial(serial_port: str) -> serial.Serial:
    """Open serial port, fixing permissions via sudo chmod if needed."""
    # Ensure we have access without running the whole script as root
    subprocess.run(["sudo", "chmod", "666", serial_port], check=True)

    ser = serial.Serial(
        port=serial_port,
        baudrate=SERIAL_BAUD,
        timeout=0.5,
    )
    print(f"[*] Serial opened: {serial_port} @ {SERIAL_BAUD}")
    return ser


def monitor_serial(ser) -> str:
    """Monitor serial via pyserial, write to log file, detect stall by silence."""
    print(f"[*] Monitoring (max {MAX_TIMEOUT}s, stall detect {STALL_TIMEOUT}s)")
    print(f"[*] Log: {LOG_FILE}")

    start = None  # set on first data received
    last_data = None

    try:
        with open(LOG_FILE, "wb") as log_fd:
            while True:
                # Timeouts only count after first data arrives
                if start is not None:
                    elapsed = time.monotonic() - start
                    if elapsed >= MAX_TIMEOUT:
                        print(f"[!] Max timeout ({MAX_TIMEOUT}s) reached")
                        return "timeout"

                    since_last = time.monotonic() - last_data
                    if since_last >= STALL_TIMEOUT:
                        print(f"[!] Stall detected ({STALL_TIMEOUT}s no output)")
                        return "stall"

                data = ser.read(ser.in_waiting or 1)
                if data:
                    now = time.monotonic()
                    if start is None:
                        print("[*] First data received, timers started")
                        start = now
                    last_data = now
                    log_fd.write(data)
                    log_fd.flush()
                    # Print decoded text (strip ANSI escapes for clean terminal)
                    text = data.decode("utf-8", errors="replace")
                    clean = text.encode("ascii", errors="replace").decode("ascii")
                    sys.stdout.write(clean)
                    sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n[!] Interrupted")
        return "interrupted"
    finally:
        ser.close()
        # Reset terminal in case escape sequences leaked
        os.system("stty sane 2>/dev/null")


def main():
    # Detect serial port
    serial_port = find_serial_port()
    if not serial_port:
        print(f"[!] Debug Probe ({DEBUG_PROBE_VID}:{DEBUG_PROBE_PID}) not found", file=sys.stderr)
        sys.exit(1)
    print(f"[*] Debug Probe found: {serial_port}")

    # Step 1: Build
    build()

    # Step 2: Storage to host
    storage_to_host()
    time.sleep(2)

    # Step 3: Flash
    flash()
    time.sleep(1)

    # Step 4: Open serial BEFORE powering Pi (capture full boot log)
    ser = open_serial(serial_port)
    ser.reset_input_buffer()

    # Step 5: Storage to Pi
    storage_to_pi()
    time.sleep(1)

    # Step 6: Power on Pi
    pi_power(1)

    # Step 7: Monitor serial
    result = monitor_serial(ser)
    print(f"[*] Boot result: {result}")

    # Step 8: Power off Pi
    pi_power(0)
    time.sleep(1)

    # Step 9: Storage back to host
    storage_to_host()
    print("[*] Ready for next session")


if __name__ == "__main__":
    main()
