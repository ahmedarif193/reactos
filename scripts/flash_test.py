#!/usr/bin/env python3
"""
Build, flash, and boot-test cycle for Raspberry Pi via USB HID relay board.

Run from the build output directory:
  cd output-Clang-arm64-debug
  ../scripts/flash_test.py

Flow:
  1. ninja livecd -> ninja reactosimg  (builds ReactOS.img in cwd)
  2. Storage -> host, Pi power off
  3. Auto-detect USB storage block device
  4. Flash ReactOS.img -> detected device
  5. Open serial before powering the Pi
  6. Storage -> Pi, then Pi 5 wake/power sequence
  7. Monitor serial, detect stall/timeout
  8. Storage -> host, Pi power off
"""

import glob
import fcntl
import json
import os
import re
import shutil
import subprocess
import sys
import time

import serial

# Build dir = cwd (the output directory you run from)
BUILD_DIR = os.getcwd()
REACTOS_IMG = os.path.join(BUILD_DIR, "ReactOS.img")

# NEVER change as AGENT AI the constants below. They describe this static local hardware setup.

# USB HID relay VID:PID used by this workflow
RELAY_VID = "5131"
RELAY_PID = "2007"

# Raspberry Pi Debug Probe / CMSIS-DAP VID:PID
DEBUG_PROBE_VID = "2e8a"
DEBUG_PROBE_PID = "000c"

# USB storage bridge VID:PID. Set these to the USB reader/adapter connected to
# the relay-controlled boot media. If empty, the script accepts one unique USB
# storage device only.
STORAGE_VID = ""
STORAGE_PID = ""

SERIAL_BAUD = 115200
STALL_TIMEOUT = 20
MAX_TIMEOUT = 80
KDBG_ABORT_DELAY = 1.0
KDBG_DUMP_CAPTURE_SECONDS = 10.0
KDBG_PROMPT_WINDOW = 1024
KDBG_STALL_BREAK_WAIT_SECONDS = 2.0

LOG_FILE = os.path.join(BUILD_DIR, "flash_test.log")
HDMI_CAPTURE_DEVICE = "/dev/v4l/by-id/usb-MACROSILICON_USB_Video-video-index0"
HDMI_CAPTURE_FILE = os.path.join(BUILD_DIR, "flash_test_hdmi.png")
HDMI_CAPTURE_FRAMES_DIR = os.path.join(BUILD_DIR, "flash_test_hdmi_frames")
HDMI_CAPTURE_LOG = os.path.join(BUILD_DIR, "flash_test_hdmi.log")
HDMI_CAPTURE_SIZE = "1920x1080"
HDMI_CAPTURE_RATE = "30"
HDMI_CAPTURE_SNAPSHOT_FPS = "2"

QUEUE_DIR = "/tmp/reactos_flash_test_hardware_queue"
QUEUE_POLL_SECONDS = 2.0
QUEUE_STATUS_SECONDS = 30.0
QUEUE_WAIT_MESSAGE = (
    "wait, another process is testing the hardware should wait a little, "
    "will let you informed once he finishes, and wait your turn"
)

KDBG_PAGER_PROMPT = "--- Press q to abort,"
KDBG_COMMAND_PROMPT = "kdb:>"
BOOT_MENU_PROMPT = "ENTER (boot)"
BOOT_TESTS_DONE_MARKER = "BOOT_TESTS_DONE"
REACTOS_BANNER_PATTERN = re.compile(r"ReactOS\s+\d")
USB_DEVICE_FOUND_PATTERN = re.compile(r"(usb\s+\S+): New USB device found, idVendor=([0-9a-fA-F]{4}), idProduct=([0-9a-fA-F]{4})")
USB_PRODUCT_PATTERN = re.compile(r"(usb\s+\S+): Product:\s*(.*)")
USB_SERIAL_PATTERN = re.compile(r"(usb\s+\S+): SerialNumber:\s*(\S+)")
KDBG_ADDRESS_PATTERNS = (
    re.compile(r"\bELR=([0-9A-Fa-f]{8,16})"),
    re.compile(r"\bAddr ([0-9A-Fa-f]{8,16})"),
    re.compile(r"\bPC=([0-9A-Fa-f]{8,16})"),
)


class FlashDevices:
    def __init__(self, relay_hidraw: str, serial_port: str, storage_dev: str | None,
                 storage_vid: str, storage_pid: str, storage_serial: str = ""):
        self.relay_hidraw = relay_hidraw
        self.serial_port = serial_port
        self.storage_dev = storage_dev
        self.storage_vid = storage_vid
        self.storage_pid = storage_pid
        self.storage_serial = storage_serial


class HdmiCaptureSession:
    def __init__(self, proc: subprocess.Popen, log_fd):
        self.proc = proc
        self.log_fd = log_fd


class HardwareQueue:
    def __init__(self, queue_dir: str = QUEUE_DIR):
        self.queue_dir = queue_dir
        self.ticket_path = None
        self.ticket_name = None
        self.ticket_number = None
        self.active_fd = None

    @property
    def state_lock_path(self) -> str:
        return os.path.join(self.queue_dir, "state.lock")

    @property
    def active_lock_path(self) -> str:
        return os.path.join(self.queue_dir, "active.lock")

    @property
    def counter_path(self) -> str:
        return os.path.join(self.queue_dir, "next-ticket")

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.release()

    def acquire(self):
        os.makedirs(self.queue_dir, mode=0o700, exist_ok=True)

        with self.lock_state():
            self.cleanup_stale_tickets()
            self.ticket_number = self.next_ticket_number()
            self.ticket_name = f"ticket-{self.ticket_number:020d}-{os.getpid()}"
            self.ticket_path = os.path.join(self.queue_dir, self.ticket_name)
            with open(self.ticket_path, "x", encoding="ascii") as f:
                json.dump(
                    {
                        "pid": os.getpid(),
                        "ticket": self.ticket_number,
                        "cwd": BUILD_DIR,
                        "argv": sys.argv,
                        "created": time.time(),
                    },
                    f,
                    sort_keys=True,
                )
                f.write("\n")
            print(f"[*] Hardware queue ticket {self.ticket_number} registered")

        waiting = False
        next_status = 0.0
        try:
            while True:
                with self.lock_state():
                    self.cleanup_stale_tickets()
                    tickets = self.ticket_names()
                    if self.ticket_name not in tickets:
                        print("[!] Hardware queue ticket disappeared", file=sys.stderr)
                        sys.exit(1)

                    position = tickets.index(self.ticket_name) + 1
                    if position == 1 and self.try_acquire_active_lock():
                        if waiting:
                            print("[*] Previous hardware test finished; your turn now")
                        else:
                            print("[*] Hardware queue acquired")
                        return

                if not waiting:
                    waiting = True
                    print(f"[*] {QUEUE_WAIT_MESSAGE}")

                now = time.monotonic()
                if now >= next_status:
                    print(f"[*] Hardware queue position {position}; {position - 1} process(es) ahead")
                    next_status = now + QUEUE_STATUS_SECONDS

                time.sleep(QUEUE_POLL_SECONDS)
        except BaseException:
            self.release()
            raise

    def release(self):
        if self.ticket_path:
            try:
                os.unlink(self.ticket_path)
            except FileNotFoundError:
                pass
            self.ticket_path = None

        if self.active_fd is not None:
            try:
                fcntl.flock(self.active_fd, fcntl.LOCK_UN)
            finally:
                os.close(self.active_fd)
                self.active_fd = None

    def lock_state(self):
        return StateLock(self.state_lock_path)

    def next_ticket_number(self) -> int:
        try:
            with open(self.counter_path, "r", encoding="ascii") as f:
                ticket = int(f.read().strip() or "1")
        except (OSError, ValueError):
            ticket = 1

        tmp_path = f"{self.counter_path}.{os.getpid()}.tmp"
        with open(tmp_path, "w", encoding="ascii") as f:
            f.write(f"{ticket + 1}\n")
        os.replace(tmp_path, self.counter_path)
        return ticket

    def ticket_names(self) -> list[str]:
        return sorted(
            name for name in os.listdir(self.queue_dir)
            if name.startswith("ticket-")
        )

    def cleanup_stale_tickets(self):
        for name in self.ticket_names():
            path = os.path.join(self.queue_dir, name)
            try:
                with open(path, "r", encoding="ascii") as f:
                    ticket = json.load(f)
            except (OSError, json.JSONDecodeError):
                continue

            pid = ticket.get("pid")
            if isinstance(pid, int) and not pid_is_alive(pid):
                try:
                    os.unlink(path)
                    print(f"[*] Removed stale hardware queue ticket from pid {pid}")
                except FileNotFoundError:
                    pass

    def try_acquire_active_lock(self) -> bool:
        if self.active_fd is None:
            self.active_fd = os.open(self.active_lock_path, os.O_CREAT | os.O_RDWR, 0o600)

        try:
            fcntl.flock(self.active_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except BlockingIOError:
            return False


class StateLock:
    def __init__(self, path: str):
        self.path = path
        self.fd = None

    def __enter__(self):
        self.fd = os.open(self.path, os.O_CREAT | os.O_RDWR, 0o600)
        fcntl.flock(self.fd, fcntl.LOCK_EX)
        return self

    def __exit__(self, exc_type, exc, tb):
        try:
            fcntl.flock(self.fd, fcntl.LOCK_UN)
        finally:
            os.close(self.fd)
            self.fd = None


def pid_is_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


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


def find_usb_parent(sysfs_path: str) -> str | None:
    path = os.path.realpath(sysfs_path)
    while path.startswith("/sys/"):
        if read_text(os.path.join(path, "idVendor")) and read_text(os.path.join(path, "idProduct")):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return None


def usb_storage_serial(block_dev: str) -> str:
    block_name = os.path.basename(block_dev)
    usb_parent = find_usb_parent(f"/sys/block/{block_name}/device")
    if not usb_parent:
        return ""
    return read_text(os.path.join(usb_parent, "serial"))


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

    print("[!] Could not uniquely identify relay-controlled USB storage", file=sys.stderr)
    if last_candidates:
        print("[*] Connected USB storage candidates:")
        for dev, vid, pid in last_candidates:
            print(f"    {dev} {vid}:{pid}")
        print("[!] Set the STORAGE_VID and STORAGE_PID constants if multiple devices are connected.", file=sys.stderr)
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
    """Find the tty device for the Debug Probe by VID:PID via sysfs."""
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
    """Storage -> host, Pi power off first."""
    print("[*] Storage -> host (Pi off)")
    relay(devices, 2, 0)
    time.sleep(2)
    relay(devices, 3, 1)
    relay(devices, 4, 1)
    time.sleep(1)


def relays_all_off(devices: FlashDevices):
    """Put the relay board into a known safe baseline state."""
    print("[*] Relay reset -> all off")
    relay(devices, 2, 0)
    relay(devices, 3, 0)
    relay(devices, 4, 0)
    time.sleep(1)


def storage_to_pi(devices: FlashDevices):
    """Storage -> Pi, then Pi power on."""
    print("[*] Storage -> Pi (Pi on)")
    relay(devices, 3, 0)
    relay(devices, 4, 0)
    time.sleep(2)
    relay(devices, 2, 1)


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


def wait_for_writable_block_device(dev: str, timeout: float = 10.0):
    deadline = time.monotonic() + timeout
    last_error = None

    while time.monotonic() < deadline:
        try:
            fd = os.open(dev, os.O_RDWR | os.O_CLOEXEC)
            os.close(fd)
            return
        except OSError as error:
            last_error = error
            time.sleep(0.2)

    raise RuntimeError(f"{dev} did not become writable: {last_error}")


def fat32_partition_serial(block_dev: str) -> int | None:
    try:
        fd = os.open(block_dev, os.O_RDONLY | os.O_CLOEXEC)
    except OSError:
        return None

    try:
        os.lseek(fd, 2048 * 512 + 0x43, os.SEEK_SET)
        data = os.read(fd, 4)
    except OSError:
        return None
    finally:
        os.close(fd)

    if len(data) != 4:
        return None

    return int.from_bytes(data, "little")


def flash(flash_target: str):
    subprocess.run(["umount", f"{flash_target}1"], stderr=subprocess.DEVNULL)
    subprocess.run(["umount", flash_target], stderr=subprocess.DEVNULL)

    print(f"[*] Waiting for write access: {flash_target}")
    try:
        wait_for_writable_block_device(flash_target)
    except RuntimeError as error:
        print(f"[!] {error}", file=sys.stderr)
        sys.exit(1)

    print(f"[*] Flashing {REACTOS_IMG} -> {flash_target}")
    proc = subprocess.run(
        ["dd", f"if={REACTOS_IMG}", f"of={flash_target}", "bs=4M",
         "conv=fsync", "status=progress"],
    )
    if proc.returncode != 0:
        print("[!] Flash failed", file=sys.stderr)
        sys.exit(1)

    subprocess.run(["sync"])
    subprocess.run(["blockdev", "--flushbufs", flash_target], stderr=subprocess.DEVNULL)
    volume_serial = fat32_partition_serial(flash_target)
    if volume_serial is not None:
        print(f"[*] Flashed FAT volume serial: 0x{volume_serial:08x}")
    power_off_storage(flash_target)
    time.sleep(2)

    print("[*] Flash complete")


def power_off_storage(flash_target: str):
    print(f"[*] Powering off host storage path: {flash_target}")
    proc = subprocess.run(
        ["udisksctl", "power-off", "-b", flash_target],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    if proc.returncode != 0:
        subprocess.run(["sg_start", "--stop", flash_target],
                       stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        try:
            with open(f"/sys/block/{os.path.basename(flash_target)}/device/delete", "w", encoding="ascii") as f:
                f.write("1\n")
        except OSError:
            pass


def open_serial(serial_port: str) -> serial.Serial:
    """Open serial port."""
    ser = serial.Serial(port=serial_port, baudrate=SERIAL_BAUD, timeout=0.5)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print(f"[*] Serial opened: {serial_port} @ {SERIAL_BAUD}")
    drain_serial_input(ser, "pre-power")
    return ser


def drain_serial_input(ser: serial.Serial, label: str, quiet_seconds: float = 0.75, max_seconds: float = 5.0):
    """Drain stale debug-probe bytes before starting a new boot capture."""
    total = 0
    deadline = time.monotonic() + max_seconds
    quiet_deadline = time.monotonic() + quiet_seconds

    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        data = ser.read(waiting or 1)
        if data:
            total += len(data)
            quiet_deadline = time.monotonic() + quiet_seconds
            continue

        if time.monotonic() >= quiet_deadline:
            break

    if total:
        print(f"[*] Drained {total} stale serial byte(s) before {label}")


def log_monitor_note(log_fd, message: str):
    """Write a monitor-side note to stdout and the serial log."""
    text = f"\n[*] {message}\n"
    sys.stdout.write(text)
    sys.stdout.flush()
    if log_fd and not log_fd.closed:
        log_fd.write(text.encode("ascii", errors="replace"))
        log_fd.flush()


def normalize_usb_serial(serial: str) -> str:
    return re.sub(r"\s+", "", serial).lower()


def track_usb_boot_storage(text: str, storage_vid: str, expected_serial: str, storage_context: dict) -> str | None:
    if not expected_serial:
        return None

    expected_vendor = storage_vid.lower()
    expected_serial = normalize_usb_serial(expected_serial)
    text = storage_context.pop("_partial_line", "") + text
    lines = text.splitlines(keepends=True)
    if lines and not lines[-1].endswith(("\n", "\r")):
        storage_context["_partial_line"] = lines.pop()

    for line in lines:
        line = line.rstrip("\r\n")
        device_match = USB_DEVICE_FOUND_PATTERN.search(line)
        if device_match:
            usb_path = device_match.group(1)
            vendor = device_match.group(2).lower()
            storage_context[usb_path] = vendor == expected_vendor

        product_match = USB_PRODUCT_PATTERN.search(line)
        if product_match:
            usb_path = product_match.group(1)
            product = product_match.group(2).lower()
            if "sandisk" in product:
                storage_context[usb_path] = True

        serial_match = USB_SERIAL_PATTERN.search(line)
        if serial_match:
            usb_path = serial_match.group(1)
            serial = normalize_usb_serial(serial_match.group(2))
            if storage_context.get(usb_path) and serial != expected_serial and not expected_serial.startswith(serial):
                return serial_match.group(2)

    return None


def find_last_kdbg_address(text: str, current: str | None) -> str | None:
    """Extract the latest fault PC-like address from the serial stream."""
    last = current
    for pattern in KDBG_ADDRESS_PATTERNS:
        for match in pattern.finditer(text):
            last = match.group(1)
    return last


def send_kdbg_command(ser, log_fd, command: str):
    """Send one KDBG command and mirror the command marker to the log."""
    log_monitor_note(log_fd, f"KDBG << {command}")
    ser.write(command.encode("ascii") + b"\r")
    ser.flush()
    time.sleep(0.25)


def send_boot_menu_enter(ser, log_fd) -> bool:
    """Accept the boot menu as soon as it is printed."""
    log_monitor_note(log_fd, "Boot menu prompt detected; sending enter")
    try:
        ser.write(b"\r")
        ser.flush()
        return True
    except (serial.SerialException, OSError) as e:
        log_monitor_note(log_fd, f"boot menu enter failed: {e}")
        return False


def kdbg_hex_address(address: str) -> str:
    """Format an address so KDBG expression parsing accepts it."""
    return address if address.lower().startswith("0x") else f"0x{address}"


def capture_kdbg_dump(ser, log_fd, reason: str, address: str | None, abort_pager: bool) -> bool:
    """Abort the KDBG pager and request a concise serial-side crash dump."""
    if abort_pager:
        log_monitor_note(log_fd, f"KDBG pager detected ({reason}); sending q")
    else:
        log_monitor_note(log_fd, f"KDBG prompt detected ({reason}); dumping state")

    try:
        if abort_pager:
            ser.write(b"q")
            ser.flush()
            time.sleep(KDBG_ABORT_DELAY)

        send_kdbg_command(ser, log_fd, "regs")
        if address:
            send_kdbg_command(ser, log_fd, f"x {kdbg_hex_address(address)} L8")
        send_kdbg_command(ser, log_fd, "bt")
        if address:
            send_kdbg_command(ser, log_fd, f"mod {kdbg_hex_address(address)}")
        return True
    except (serial.SerialException, OSError) as e:
        log_monitor_note(log_fd, f"KDBG dump failed: {e}")
        return False


def drain_serial_for(ser, log_fd, seconds: float):
    """Drain serial output for a bounded time and mirror it to stdout/log."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            data = ser.read(ser.in_waiting or 1)
        except (serial.SerialException, OSError):
            time.sleep(0.5)
            continue

        if not data:
            continue

        if log_fd and not log_fd.closed:
            log_fd.write(data)
            log_fd.flush()

        text = data.decode("utf-8", errors="replace")
        clean = text.encode("ascii", errors="replace").decode("ascii")
        sys.stdout.write(clean)
        sys.stdout.flush()


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


def start_hdmi_capture_stream() -> HdmiCaptureSession | None:
    """Keep the HDMI capture stream warm and update one latest-frame PNG."""
    if not os.path.exists(HDMI_CAPTURE_DEVICE):
        print(f"[*] HDMI capture device not found: {HDMI_CAPTURE_DEVICE}")
        return None

    if not shutil.which("ffmpeg"):
        print("[*] ffmpeg not found; HDMI screenshot capture disabled")
        return None

    try:
        os.unlink(HDMI_CAPTURE_FILE)
    except FileNotFoundError:
        pass
    except OSError as e:
        print(f"[!] Could not remove old HDMI screenshot: {e}", file=sys.stderr)

    # Write a numbered sequence of complete frames rather than continuously
    # rewriting a single PNG in place. Rewriting in place can leave a 0-byte
    # file if ffmpeg is terminated mid-write; numbered frames are always
    # complete, so the latest one can be used as the final screenshot and the
    # earlier ones preserve the boot progression for review.
    try:
        if os.path.isdir(HDMI_CAPTURE_FRAMES_DIR):
            for stale in glob.glob(os.path.join(HDMI_CAPTURE_FRAMES_DIR, "*.png")):
                try:
                    os.unlink(stale)
                except OSError:
                    pass
        else:
            os.makedirs(HDMI_CAPTURE_FRAMES_DIR, exist_ok=True)
    except OSError as e:
        print(f"[!] Could not prepare HDMI frames dir: {e}", file=sys.stderr)

    try:
        log_fd = open(HDMI_CAPTURE_LOG, "wb")
    except OSError as e:
        print(f"[!] Could not open HDMI capture log: {e}", file=sys.stderr)
        return None

    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "warning",
        "-y",
        "-f",
        "v4l2",
        "-input_format",
        "mjpeg",
        "-video_size",
        HDMI_CAPTURE_SIZE,
        "-framerate",
        HDMI_CAPTURE_RATE,
        "-i",
        HDMI_CAPTURE_DEVICE,
        "-vf",
        f"fps={HDMI_CAPTURE_SNAPSHOT_FPS}",
        os.path.join(HDMI_CAPTURE_FRAMES_DIR, "frame_%05d.png"),
    ]

    try:
        proc = subprocess.Popen(
            cmd,
            cwd=BUILD_DIR,
            stdin=subprocess.DEVNULL,
            stdout=log_fd,
            stderr=subprocess.STDOUT,
        )
    except OSError as e:
        log_fd.close()
        print(f"[!] Could not start HDMI capture: {e}", file=sys.stderr)
        return None

    time.sleep(0.25)
    if proc.poll() is not None:
        log_fd.close()
        print(f"[!] HDMI capture exited early with status {proc.returncode}; see {HDMI_CAPTURE_LOG}", file=sys.stderr)
        return None

    print(f"[*] HDMI capture warming: {HDMI_CAPTURE_DEVICE} -> {HDMI_CAPTURE_FILE}")
    return HdmiCaptureSession(proc, log_fd)


def stop_hdmi_capture_stream(session: HdmiCaptureSession | None):
    """Stop the warmed HDMI stream and leave the last updated frame on disk."""
    if session is None:
        return

    if session.proc.poll() is None:
        session.proc.terminate()
        try:
            session.proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            session.proc.kill()
            session.proc.wait(timeout=3.0)

    session.log_fd.close()

    # Pick the most recent complete frame as the final screenshot.
    frames = sorted(glob.glob(os.path.join(HDMI_CAPTURE_FRAMES_DIR, "frame_*.png")))
    latest = None
    for candidate in reversed(frames):
        try:
            if os.path.getsize(candidate) > 0:
                latest = candidate
                break
        except OSError:
            continue

    if latest is not None:
        try:
            shutil.copyfile(latest, HDMI_CAPTURE_FILE)
        except OSError as e:
            print(f"[!] Could not copy latest HDMI frame: {e}", file=sys.stderr)

    if os.path.exists(HDMI_CAPTURE_FILE) and os.path.getsize(HDMI_CAPTURE_FILE) > 0:
        size = os.path.getsize(HDMI_CAPTURE_FILE)
        print(f"[*] HDMI screenshot: {HDMI_CAPTURE_FILE} ({size} bytes, {len(frames)} frames in {HDMI_CAPTURE_FRAMES_DIR})")
    else:
        print(f"[!] HDMI screenshot was not produced; see {HDMI_CAPTURE_LOG}", file=sys.stderr)


def monitor_serial(ser, expected_storage_serial: str, expected_storage_vid: str) -> str:
    """Monitor serial, write to log file, detect stall by silence."""
    print(f"[*] Monitoring (max {MAX_TIMEOUT}s, stall detect {STALL_TIMEOUT}s)")
    print(f"[*] Log: {LOG_FILE}")
    if expected_storage_serial:
        print(f"[*] Expecting boot USB serial: {expected_storage_serial}")

    start = time.monotonic()
    last_data = start
    recent_text = ""
    last_kdbg_address = None
    storage_context = {}
    boot_menu_enter_sent = False
    kdbg_dumped = False
    kdbg_dump_deadline = None
    log_fd = None

    try:
        with open(LOG_FILE, "wb") as log_fd:
            while True:
                if kdbg_dump_deadline is not None and time.monotonic() >= kdbg_dump_deadline:
                    print("[*] KDBG dump capture window complete")
                    return "kdbg-dump"

                elapsed = time.monotonic() - start
                if elapsed >= MAX_TIMEOUT:
                    print(f"[!] Max timeout ({MAX_TIMEOUT}s) reached")
                    return "timeout"

                since_last = time.monotonic() - last_data
                if since_last >= STALL_TIMEOUT:
                    print(f"[!] Stall detected ({STALL_TIMEOUT}s no output)")
                    log_monitor_note(log_fd, "Sending serial break for KDBG stall dump")
                    try:
                        ser.write(b"\x03")
                        ser.flush()
                        ser.send_break(duration=0.25)
                    except (serial.SerialException, OSError):
                        return "stall"

                    saw_break_data = False
                    break_deadline = time.monotonic() + KDBG_STALL_BREAK_WAIT_SECONDS
                    while time.monotonic() < break_deadline:
                        try:
                            data = ser.read(ser.in_waiting or 1)
                        except (serial.SerialException, OSError):
                            break

                        if not data:
                            continue

                        saw_break_data = True
                        last_data = time.monotonic()
                        log_fd.write(data)
                        log_fd.flush()
                        text = data.decode("utf-8", errors="replace")
                        clean = text.encode("ascii", errors="replace").decode("ascii")
                        sys.stdout.write(clean)
                        sys.stdout.flush()

                        recent_text = (recent_text + clean)[-KDBG_PROMPT_WINDOW:]
                        last_kdbg_address = find_last_kdbg_address(recent_text, last_kdbg_address)
                        kdbg_pager_seen = KDBG_PAGER_PROMPT in recent_text
                        kdbg_prompt_seen = KDBG_COMMAND_PROMPT in recent_text
                        if kdbg_pager_seen or kdbg_prompt_seen:
                            reason = "serial break at stall pager prompt" if kdbg_pager_seen else "serial break at stall debugger prompt"
                            if capture_kdbg_dump(ser, log_fd, reason, last_kdbg_address, kdbg_pager_seen):
                                drain_serial_for(ser, log_fd, KDBG_DUMP_CAPTURE_SECONDS)
                                print("[*] KDBG dump capture window complete")
                                return "kdbg-dump"

                    if saw_break_data:
                        continue

                    return "stall"

                try:
                    data = ser.read(ser.in_waiting or 1)
                except (serial.SerialException, OSError) as e:
                    log_monitor_note(log_fd, f"serial read failed: {e}")
                    return "serial-error"

                if data:
                    now = time.monotonic()
                    if last_data == start:
                        print("[*] First data received, timers started")
                    last_data = now
                    log_fd.write(data)
                    log_fd.flush()
                    text = data.decode("utf-8", errors="replace")
                    clean = text.encode("ascii", errors="replace").decode("ascii")
                    sys.stdout.write(clean)
                    sys.stdout.flush()

                    recent_text = (recent_text + clean)[-KDBG_PROMPT_WINDOW:]

                    wrong_serial = track_usb_boot_storage(clean, expected_storage_vid, expected_storage_serial, storage_context)
                    if wrong_serial:
                        log_monitor_note(log_fd, f"boot media mismatch: flashed serial {expected_storage_serial}, boot serial {wrong_serial}")
                        return "wrong-media"

                    if not boot_menu_enter_sent and BOOT_MENU_PROMPT in recent_text:
                        boot_menu_enter_sent = True
                        if not send_boot_menu_enter(ser, log_fd):
                            return "serial-error"

                    if BOOT_TESTS_DONE_MARKER in recent_text:
                        print("[*] Boot tests completed")
                        return "success"

                    last_kdbg_address = find_last_kdbg_address(recent_text, last_kdbg_address)
                    kdbg_pager_seen = KDBG_PAGER_PROMPT in recent_text
                    kdbg_prompt_seen = KDBG_COMMAND_PROMPT in recent_text
                    if not kdbg_dumped and (kdbg_pager_seen or kdbg_prompt_seen):
                        reason = "serial pager prompt" if kdbg_pager_seen else "serial debugger prompt"
                        kdbg_dumped = capture_kdbg_dump(ser, log_fd, reason, last_kdbg_address, kdbg_pager_seen)
                        if kdbg_dumped:
                            kdbg_dump_deadline = time.monotonic() + KDBG_DUMP_CAPTURE_SECONDS
    except KeyboardInterrupt:
        kdbg_pager_seen = KDBG_PAGER_PROMPT in recent_text
        kdbg_prompt_seen = KDBG_COMMAND_PROMPT in recent_text
        if not kdbg_dumped and (kdbg_pager_seen or kdbg_prompt_seen):
            with open(LOG_FILE, "ab") as interrupt_log_fd:
                reason = "keyboard interrupt at pager prompt" if kdbg_pager_seen else "keyboard interrupt at debugger prompt"
                if capture_kdbg_dump(ser, interrupt_log_fd, reason, last_kdbg_address, kdbg_pager_seen):
                    drain_serial_for(ser, interrupt_log_fd, KDBG_DUMP_CAPTURE_SECONDS)
                    print("[*] KDBG dump capture window complete")
                    return "kdbg-dump"
        print("\n[!] Interrupted")
        return "interrupted"
    finally:
        ser.close()
        os.system("stty sane 2>/dev/null")


def run_flash_test():
    devices = discover_devices()
    relays_all_off(devices)

    build()

    storage_to_host(devices)
    if devices.storage_vid and devices.storage_pid:
        devices.storage_dev = wait_for_usb_storage_by_vidpid(devices.storage_vid, devices.storage_pid)
    else:
        devices.storage_dev, devices.storage_vid, devices.storage_pid = wait_for_single_usb_storage()
    devices.storage_serial = usb_storage_serial(devices.storage_dev)
    if devices.storage_serial:
        print(f"[*] Flash target identity: {devices.storage_dev} {devices.storage_vid}:{devices.storage_pid} serial={devices.storage_serial}")
    else:
        print(f"[!] Could not read USB serial for {devices.storage_dev}", file=sys.stderr)
    flash(devices.storage_dev)
    time.sleep(1)

    hdmi_capture = None

    try:
        ser = open_serial(devices.serial_port)
        ser.reset_input_buffer()

        hdmi_capture = start_hdmi_capture_stream()
        storage_to_pi(devices)

        result = monitor_serial(ser, devices.storage_serial, devices.storage_vid)
        print(f"[*] Boot result: {result}")
    finally:
        stop_hdmi_capture_stream(hdmi_capture)
        storage_to_host(devices)

    print("[*] Ready for next session")


def main():
    with HardwareQueue():
        run_flash_test()


if __name__ == "__main__":
    main()
