#!/usr/bin/env python3
"""
Build, flash, and boot-test cycle for Raspberry Pi via serial reset trigger.

Run from the build output directory:
  cd output-Clang-amd64-debug
  ../scripts/flash_test_serial_reset.py

Flow:
  1. ninja livecd -> ninja reactosimg  (builds ReactOS.img in cwd)
  2. Storage -> host through the relay board
  3. Auto-detect USB storage block device
  4. Flash ReactOS.img -> detected device
  5. Flush host storage buffers
  6. Open serial before switching storage back
  7. Storage -> Pi through the relay board
  8. Send serial reset trigger, then monitor serial output
  9. Storage -> host for the next session
"""

import glob
import os
import subprocess
import sys
import time

import serial

# Build dir = cwd (the output directory you run from)
BUILD_DIR = os.getcwd()
REACTOS_IMG = os.path.join(BUILD_DIR, "ReactOS.img")

# USB HID relay VID:PID used for storage switching
RELAY_VID = os.environ.get("ROS_FLASH_RELAY_VID", "5131").lower()
RELAY_PID = os.environ.get("ROS_FLASH_RELAY_PID", "2007").lower()

# Raspberry Pi Debug Probe / CMSIS-DAP VID:PID
DEBUG_PROBE_VID = os.environ.get("ROS_FLASH_SERIAL_VID", "2e8a").lower()
DEBUG_PROBE_PID = os.environ.get("ROS_FLASH_SERIAL_PID", "000c").lower()

# USB storage bridge VID:PID. Set these to the USB reader/adapter connected to
# the boot media. If unset, the script refuses to guess.
STORAGE_VID = os.environ.get("ROS_FLASH_STORAGE_VID", "").lower()
STORAGE_PID = os.environ.get("ROS_FLASH_STORAGE_PID", "").lower()

SERIAL_BAUD = 115200
SERIAL_PORT = os.environ.get("ROS_FLASH_SERIAL_PORT", "")
RESET_TEXT = os.environ.get("ROS_FLASH_RESET_TEXT", "\u00a7")
STALL_TIMEOUT = int(os.environ.get("ROS_FLASH_STALL_TIMEOUT", "20"))
MAX_TIMEOUT = int(os.environ.get("ROS_FLASH_MAX_TIMEOUT", "2600"))

LOG_FILE = os.path.join(BUILD_DIR, "flash_test_serial_reset.log")


class FlashDevices:
    def __init__(self, relay_hidraw: str, serial_port: str, storage_dev: str | None, storage_vid: str, storage_pid: str):
        self.relay_hidraw = relay_hidraw
        self.serial_port = serial_port
        self.storage_dev = storage_dev
        self.storage_vid = storage_vid
        self.storage_pid = storage_pid


def read_text(path: str) -> str:
    try:
        with open(path, "r", encoding="ascii", errors="ignore") as f:
            return f.read().strip()
    except OSError:
        return ""


def usb_device_matches(sysfs_path: str, vid: str, pid: str) -> bool:
    path = os.path.realpath(sysfs_path)
    while path.startswith("/sys/"):
        if (read_text(os.path.join(path, "idVendor")).lower() == vid and
            read_text(os.path.join(path, "idProduct")).lower() == pid):
            return True
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return False


def find_usb_parent_with_vidpid(sysfs_path: str) -> tuple[str, str] | None:
    path = os.path.realpath(sysfs_path)
    while path.startswith("/sys/"):
        vid = read_text(os.path.join(path, "idVendor")).lower()
        pid = read_text(os.path.join(path, "idProduct")).lower()
        if vid and pid:
            return vid, pid
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return None


def find_hidraw_device(vid: str = RELAY_VID, pid: str = RELAY_PID) -> str | None:
    """Find the relay hidraw device by USB VID:PID."""
    for entry in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        device_path = os.path.join(entry, "device")
        if usb_device_matches(device_path, vid, pid):
            dev_name = os.path.basename(entry)
            dev_path = f"/dev/{dev_name}"
            if os.path.exists(dev_path):
                return dev_path
    return None


def relay(devices: FlashDevices, pin: int, state: int):
    """Control relay using the 0xA0,pin,state,checksum HID frame."""
    header = 0xA0
    checksum = (header + pin + state) & 0xFF
    cmd = bytes([header, pin, state, checksum])

    try:
        with open(devices.relay_hidraw, "wb", buffering=0) as f:
            f.write(cmd)
    except OSError as e:
        print(f"[!] Failed to write relay command to {devices.relay_hidraw}: {e}", file=sys.stderr)
        sys.exit(1)

    label = "ON" if state else "OFF"
    print(f"  relay {pin} -> {label}")


def find_usb_storage_by_vidpid(vid: str, pid: str) -> str | None:
    """Find the USB mass storage block device matching VID:PID."""
    if not vid or not pid:
        return None

    for entry in sorted(os.listdir("/sys/block")):
        if not entry.startswith("sd"):
            continue
        device_path = f"/sys/block/{entry}/device"
        real_path = os.path.realpath(device_path)
        if "/usb" not in real_path:
            continue
        if not usb_device_matches(device_path, vid, pid):
            continue
        dev_path = f"/dev/{entry}"
        if os.path.exists(dev_path):
            return dev_path
    return None


def wait_for_usb_storage_by_vidpid(vid: str, pid: str, timeout: int = 10) -> str:
    """Wait for a USB mass storage block device matching VID:PID."""
    print(f"[*] Waiting for USB storage {vid}:{pid} (up to {timeout}s)...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        dev = find_usb_storage_by_vidpid(vid, pid)
        if dev:
            print(f"[*] USB storage found: {dev}")
            return dev
        time.sleep(0.5)
    print(f"[!] USB storage {vid}:{pid} not found", file=sys.stderr)
    sys.exit(1)


def wait_for_single_usb_storage(timeout: int = 10) -> tuple[str, str, str]:
    """Wait for exactly one USB storage block device and return (dev, vid, pid)."""
    print(f"[*] Waiting for USB storage device (up to {timeout}s)...")
    deadline = time.monotonic() + timeout
    last_candidates = []
    while time.monotonic() < deadline:
        candidates = [
            candidate for candidate in list_usb_storage_candidates()
            if candidate[1] != "unknown" and candidate[2] != "unknown"
        ]
        last_candidates = candidates
        if len(candidates) == 1:
            dev, vid, pid = candidates[0]
            print(f"[*] USB storage found: {dev} {vid}:{pid}")
            return dev, vid, pid
        if len(candidates) > 1:
            break
        time.sleep(0.5)

    print("[!] Could not uniquely identify USB boot storage", file=sys.stderr)
    if last_candidates:
        print("[*] Connected USB storage candidates:")
        for dev, vid, pid in last_candidates:
            print(f"    {dev} {vid}:{pid}")
        print("[!] Set ROS_FLASH_STORAGE_VID and ROS_FLASH_STORAGE_PID if multiple devices are connected.", file=sys.stderr)
    sys.exit(1)


def list_usb_storage_candidates() -> list[tuple[str, str, str]]:
    """Return USB storage candidates as (device, vid, pid)."""
    candidates = []
    for entry in sorted(os.listdir("/sys/block")):
        if not entry.startswith("sd"):
            continue
        device_path = f"/sys/block/{entry}/device"
        real_path = os.path.realpath(device_path)
        if "/usb" not in real_path:
            continue
        vidpid = find_usb_parent_with_vidpid(device_path)
        if vidpid:
            candidates.append((f"/dev/{entry}", vidpid[0], vidpid[1]))
        else:
            candidates.append((f"/dev/{entry}", "unknown", "unknown"))
    return candidates


def find_serial_port() -> str | None:
    """Find the serial device by USB VID:PID, with an explicit override."""
    if SERIAL_PORT:
        return SERIAL_PORT if os.path.exists(SERIAL_PORT) else None
    for dev in glob.glob("/sys/bus/usb/devices/*/"):
        if not usb_device_matches(dev, DEBUG_PROBE_VID, DEBUG_PROBE_PID):
            continue
        for tty in glob.glob(os.path.join(dev, "*/tty/tty*")):
            tty_name = os.path.basename(tty)
            tty_path = f"/dev/{tty_name}"
            if os.path.exists(tty_path):
                return tty_path
    return None


def storage_to_host(devices: FlashDevices):
    """Storage -> host. Device reset/power is left to the serial trigger."""
    print("[*] Storage -> host")
    relay(devices, 3, 1)
    relay(devices, 4, 1)


def storage_to_pi(devices: FlashDevices):
    """Storage -> Pi. Device reset/power is left to the serial trigger."""
    print("[*] Storage -> Pi")
    relay(devices, 3, 0)
    relay(devices, 4, 0)


def build():
    print(f"[*] Build dir: {BUILD_DIR}")

    print("[*] Building livecd...")
    proc = subprocess.run(["ninja", "livecd"], cwd=BUILD_DIR)
    if proc.returncode != 0:
        print("[!] ninja livecd failed", file=sys.stderr)
        sys.exit(1)

    print("[*] Building reactosimg...")
    proc = subprocess.run(["ninja", "reactosimg"], cwd=BUILD_DIR)
    if proc.returncode != 0:
        print("[!] ninja reactosimg failed", file=sys.stderr)
        sys.exit(1)

    if not os.path.exists(REACTOS_IMG):
        print(f"[!] {REACTOS_IMG} not found after build", file=sys.stderr)
        sys.exit(1)

    print(f"[*] Build complete: {REACTOS_IMG}")


def flash(flash_target: str):
    subprocess.run(["umount", f"{flash_target}1"], stderr=subprocess.DEVNULL)
    subprocess.run(["umount", flash_target], stderr=subprocess.DEVNULL)

    print(f"[*] Flashing {REACTOS_IMG} -> {flash_target}")
    proc = subprocess.run(
        ["dd", f"if={REACTOS_IMG}", f"of={flash_target}", "bs=4M",
         "status=progress", "oflag=sync"],
    )
    if proc.returncode != 0:
        print("[!] Flash failed", file=sys.stderr)
        sys.exit(1)

    subprocess.run(["sync"])
    subprocess.run(["blockdev", "--flushbufs", flash_target], stderr=subprocess.DEVNULL)

    print("[*] Flash complete")


def kill_port_holder(port_path: str):
    """Find and kill the process holding a serial port, matching hw_monitor.py."""
    try:
        result = subprocess.run(
            ["fuser", port_path],
            capture_output=True, timeout=5,
        )
        pids = result.stdout.decode(errors="ignore").split()
        if not pids:
            pids = result.stderr.decode(errors="ignore").split()
        pids = [p.strip().rstrip("m") for p in pids if p.strip().rstrip("m").isdigit()]
        if not pids:
            return
        for pid in pids:
            print(f"[*] Killing PID {pid} holding {port_path}")
            subprocess.run(["kill", "-9", pid], timeout=5)
        time.sleep(0.5)
    except Exception as e:
        print(f"[!] Failed to kill serial port holder: {e}", file=sys.stderr)


def open_serial(port_path: str) -> serial.Serial:
    """Open the serial port the same way as hw_monitor.py."""
    kill_port_holder(port_path)
    try:
        ser = serial.Serial(
            port=port_path,
            baudrate=SERIAL_BAUD,
            timeout=0.5,
            write_timeout=2,
        )
    except Exception as e:
        print(f"[!] Error opening serial port {port_path}: {e}", file=sys.stderr)
        sys.exit(1)
    print(f"[*] Serial opened: {port_path} @ {SERIAL_BAUD}")
    return ser


def send_serial_reset(ser):
    """Send the device-side reset trigger over the serial port."""
    try:
        ser.write(RESET_TEXT.encode("utf-8"))
        ser.flush()
    except Exception as e:
        print(f"[!] Error sending reset: {e}", file=sys.stderr)
        sys.exit(1)
    print(f"[*] Reset command sent ({RESET_TEXT!r})")


def discover_devices() -> FlashDevices:
    """Discover and validate all hardware paths before starting the cycle."""
    relay_hidraw = find_hidraw_device()
    serial_port = find_serial_port()
    storage_vid = STORAGE_VID
    storage_pid = STORAGE_PID
    storage_dev = find_usb_storage_by_vidpid(storage_vid, storage_pid)

    if not storage_vid or not storage_pid:
        candidates = [
            candidate for candidate in list_usb_storage_candidates()
            if candidate[1] != "unknown" and candidate[2] != "unknown"
        ]
        if len(candidates) == 1:
            storage_dev, storage_vid, storage_pid = candidates[0]

    print("[*] Device discovery:")
    print(f"    relay {RELAY_VID}:{RELAY_PID}: {relay_hidraw or 'not found'}")
    print(f"    serial {DEBUG_PROBE_VID}:{DEBUG_PROBE_PID}: {serial_port or 'not found'}")
    storage_label = f"{storage_vid}:{storage_pid}" if storage_vid and storage_pid else "unset"
    print(f"    storage {storage_label}: {storage_dev or 'not visible yet'}")

    missing = False
    if not relay_hidraw:
        print(f"[!] Relay board {RELAY_VID}:{RELAY_PID} not found", file=sys.stderr)
        missing = True
    if not serial_port:
        print(f"[!] Debug Probe {DEBUG_PROBE_VID}:{DEBUG_PROBE_PID} not found", file=sys.stderr)
        missing = True

    if missing:
        sys.exit(1)

    return FlashDevices(relay_hidraw, serial_port, storage_dev, storage_vid, storage_pid)


def monitor_serial(ser) -> str:
    """Read serial output like hw_monitor.py: raw bytes to log/stdout."""
    print(f"[*] Monitoring (max {MAX_TIMEOUT}s, stall detect {STALL_TIMEOUT}s)")
    print(f"[*] Log: {LOG_FILE}")

    start = time.monotonic()
    last_data = start

    try:
        with open(LOG_FILE, "wb") as log_fd:
            while True:
                elapsed = time.monotonic() - start
                if elapsed >= MAX_TIMEOUT:
                    print(f"[!] Max timeout ({MAX_TIMEOUT}s) reached")
                    return "timeout"

                try:
                    data = ser.read(4096)
                except Exception as e:
                    print(f"[!] Serial read error: {e}", file=sys.stderr)
                    return "serial-error"

                if data:
                    log_fd.write(data)
                    log_fd.flush()
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                    last_data = time.monotonic()
                    continue

                since_last = time.monotonic() - last_data
                if since_last >= STALL_TIMEOUT:
                    print(f"[!] Stall detected ({STALL_TIMEOUT}s no output)")
                    return "stall"
    except KeyboardInterrupt:
        print("\n[!] Interrupted")
        return "interrupted"
    finally:
        ser.close()


def main():
    devices = discover_devices()

    build()

    storage_to_host(devices)
    if devices.storage_vid and devices.storage_pid:
        devices.storage_dev = wait_for_usb_storage_by_vidpid(devices.storage_vid, devices.storage_pid)
    else:
        devices.storage_dev, devices.storage_vid, devices.storage_pid = wait_for_single_usb_storage()
    flash(devices.storage_dev)

    ser = open_serial(devices.serial_port)

    storage_to_pi(devices)
    send_serial_reset(ser)

    result = monitor_serial(ser)
    print(f"[*] Boot result: {result}")

    storage_to_host(devices)
    print("[*] Ready for next session")


if __name__ == "__main__":
    main()
