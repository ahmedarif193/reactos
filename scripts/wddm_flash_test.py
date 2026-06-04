#!/usr/bin/env python3
"""
NO-REBUILD flash + boot + HDMI capture harness for the Raspberry Pi 5.

This is a SEPARATE harness from scripts/flash_test.py.  flash_test.py runs its
own `ninja livecd -> ninja reactosimg` every invocation, which makes an external
"reverter" drop the tracked WDDM wiring before the image is built.  To validate
a SPECIFIC, atomically-built ReactOS.img (e.g. one built with the rpi5dod ROOT
seed registered so the desktop composites by default), we must flash THAT image
WITHOUT triggering another build.

This harness therefore:
  * does NOT build anything -- it flashes the EXISTING ReactOS.img in the build
    directory (build it yourself first, atomically:
        RPI5DOD_ROOT=1 scripts/wddm_reapply_wiring.sh && cmake . \
            && ninja livecd && ninja reactosimg);
  * reuses flash_test.py's PROVEN relay / serial / HDMI / hardware-queue logic
    (the VID:PIDs, capture device and HDMI settings describe the SAME static
    local hardware, so they are intentionally identical -- this file does not
    edit flash_test.py or its constants);
  * keeps capturing serial + HDMI through the quiet desktop period instead of
    bailing on the benign "stall" false-positive, so a longer compositor run
    (and its frame-rate timestamps) can be observed.

Usage (run from the build output directory, e.g. output-Clang-arm64-debug):
    ../scripts/wddm_flash_test.py [monitor_seconds]
      monitor_seconds : how long to monitor serial after power-on (default 120).

It cooperates with flash_test.py via the same hardware queue, so only one test
drives the board at a time.
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

# ---------------------------------------------------------------------------
# Hardware identity.  These mirror scripts/flash_test.py because they describe
# the SAME static local hardware setup (relay board, debug probe, HDMI grabber).
# Do NOT change them; they are not "this harness's" constants to tune.
# ---------------------------------------------------------------------------
RELAY_VID = "5131"
RELAY_PID = "2007"
DEBUG_PROBE_VID = "2e8a"
DEBUG_PROBE_PID = "000c"
STORAGE_VID = ""
STORAGE_PID = ""

SERIAL_BAUD = 115200

# Monitor cadence.  This harness deliberately keeps watching through the quiet
# desktop period (the boot heartbeat was removed upstream, so serial goes silent
# once the desktop settles; flash_test.py treats that as "stall" and powers off).
# Default 120s so the compositor's steady-state present rate can be timed.
DEFAULT_MONITOR_SECONDS = 120
KDBG_PROMPT_WINDOW = 1024
KDBG_DUMP_CAPTURE_SECONDS = 10.0

# Separate log/capture filenames so a wddm run does not clobber a flash_test run.
LOG_FILE = os.path.join(BUILD_DIR, "wddm_flash_test.log")
HDMI_CAPTURE_DEVICE = "/dev/v4l/by-id/usb-MACROSILICON_USB_Video-video-index0"
HDMI_CAPTURE_FILE = os.path.join(BUILD_DIR, "wddm_flash_test_hdmi.png")
HDMI_CAPTURE_FRAMES_DIR = os.path.join(BUILD_DIR, "wddm_flash_test_hdmi_frames")
HDMI_CAPTURE_LOG = os.path.join(BUILD_DIR, "wddm_flash_test_hdmi.log")
HDMI_CAPTURE_SIZE = "1920x1080"
HDMI_CAPTURE_RATE = "30"
HDMI_CAPTURE_SNAPSHOT_FPS = "2"

# Shared with flash_test.py so the two harnesses serialize on the board.
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
USB_DEVICE_FOUND_PATTERN = re.compile(r"(usb\s+\S+): New USB device found, idVendor=([0-9a-fA-F]{4}), idProduct=([0-9a-fA-F]{4})")


class FlashDevices:
    def __init__(self, relay_hidraw, serial_port, storage_dev, storage_vid, storage_pid, storage_serial=""):
        self.relay_hidraw = relay_hidraw
        self.serial_port = serial_port
        self.storage_dev = storage_dev
        self.storage_vid = storage_vid
        self.storage_pid = storage_pid
        self.storage_serial = storage_serial


class HdmiCaptureSession:
    def __init__(self, proc, log_fd):
        self.proc = proc
        self.log_fd = log_fd


# ---------------------------------------------------------------------------
# Hardware queue (identical semantics to flash_test.py so both serialize).
# ---------------------------------------------------------------------------
class HardwareQueue:
    def __init__(self, queue_dir=QUEUE_DIR):
        self.queue_dir = queue_dir
        self.ticket_path = None
        self.ticket_name = None
        self.ticket_number = None
        self.active_fd = None

    @property
    def state_lock_path(self):
        return os.path.join(self.queue_dir, "state.lock")

    @property
    def active_lock_path(self):
        return os.path.join(self.queue_dir, "active.lock")

    @property
    def counter_path(self):
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
                json.dump({"pid": os.getpid(), "ticket": self.ticket_number,
                           "cwd": BUILD_DIR, "argv": sys.argv, "created": time.time()},
                          f, sort_keys=True)
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

    def next_ticket_number(self):
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

    def ticket_names(self):
        return sorted(name for name in os.listdir(self.queue_dir) if name.startswith("ticket-"))

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

    def try_acquire_active_lock(self):
        if self.active_fd is None:
            self.active_fd = os.open(self.active_lock_path, os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(self.active_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except BlockingIOError:
            return False


class StateLock:
    def __init__(self, path):
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


def pid_is_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


# ---------------------------------------------------------------------------
# USB / sysfs helpers (copied verbatim from flash_test.py -- same hardware).
# ---------------------------------------------------------------------------
def read_text(path):
    try:
        with open(path, "r", encoding="ascii", errors="ignore") as f:
            return f.read().strip()
    except OSError:
        return ""


def usb_device_matches(sysfs_path, vid, pid):
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


def find_usb_parent_with_vidpid(sysfs_path):
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


def find_usb_parent(sysfs_path):
    path = os.path.realpath(sysfs_path)
    while path.startswith("/sys/"):
        if read_text(os.path.join(path, "idVendor")) and read_text(os.path.join(path, "idProduct")):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            break
        path = parent
    return None


def usb_storage_serial(block_dev):
    block_name = os.path.basename(block_dev)
    usb_parent = find_usb_parent(f"/sys/block/{block_name}/device")
    if not usb_parent:
        return ""
    return read_text(os.path.join(usb_parent, "serial"))


def find_hidraw_device(vid=RELAY_VID, pid=RELAY_PID):
    for entry in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        device_path = os.path.join(entry, "device")
        if usb_device_matches(device_path, vid, pid):
            dev_name = os.path.basename(entry)
            dev_path = f"/dev/{dev_name}"
            if os.path.exists(dev_path):
                return dev_path
    return None


def relay(devices, pin, state):
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


def find_usb_storage_by_vidpid(vid, pid):
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


def wait_for_usb_storage_by_vidpid(vid, pid, timeout=10):
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


def list_usb_storage_candidates():
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


def wait_for_single_usb_storage(timeout=10):
    print(f"[*] Waiting for USB storage device (up to {timeout}s)...")
    deadline = time.monotonic() + timeout
    last_candidates = []
    while time.monotonic() < deadline:
        candidates = [c for c in list_usb_storage_candidates()
                      if c[1] != "unknown" and c[2] != "unknown"]
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
    sys.exit(1)


def find_serial_port():
    for dev in glob.glob("/sys/bus/usb/devices/*/"):
        if not usb_device_matches(dev, DEBUG_PROBE_VID, DEBUG_PROBE_PID):
            continue
        for tty in glob.glob(os.path.join(dev, "*/tty/tty*")):
            tty_name = os.path.basename(tty)
            tty_path = f"/dev/{tty_name}"
            if os.path.exists(tty_path):
                return tty_path
    return None


# ---------------------------------------------------------------------------
# Relay choreography (identical pin sequence to flash_test.py).
# ---------------------------------------------------------------------------
def storage_to_host(devices):
    print("[*] Storage -> host (Pi off)")
    relay(devices, 2, 0)
    time.sleep(2)
    relay(devices, 3, 1)
    relay(devices, 4, 1)
    time.sleep(1)


def relays_all_off(devices):
    print("[*] Relay reset -> all off")
    relay(devices, 2, 0)
    relay(devices, 3, 0)
    relay(devices, 4, 0)
    time.sleep(1)


def storage_to_pi(devices):
    print("[*] Storage -> Pi (Pi on)")
    relay(devices, 3, 0)
    relay(devices, 4, 0)
    time.sleep(2)
    relay(devices, 2, 1)


def wait_for_writable_block_device(dev, timeout=10.0):
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


def fat32_partition_serial(block_dev):
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


def power_off_storage(flash_target):
    print(f"[*] Powering off host storage path: {flash_target}")
    proc = subprocess.run(["udisksctl", "power-off", "-b", flash_target],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if proc.returncode != 0:
        subprocess.run(["sg_start", "--stop", flash_target],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            with open(f"/sys/block/{os.path.basename(flash_target)}/device/delete", "w", encoding="ascii") as f:
                f.write("1\n")
        except OSError:
            pass


def flash(flash_target):
    subprocess.run(["umount", f"{flash_target}1"], stderr=subprocess.DEVNULL)
    subprocess.run(["umount", flash_target], stderr=subprocess.DEVNULL)
    print(f"[*] Waiting for write access: {flash_target}")
    try:
        wait_for_writable_block_device(flash_target)
    except RuntimeError as error:
        print(f"[!] {error}", file=sys.stderr)
        sys.exit(1)
    print(f"[*] Flashing {REACTOS_IMG} -> {flash_target}")
    proc = subprocess.run(["dd", f"if={REACTOS_IMG}", f"of={flash_target}", "bs=4M",
                           "conv=fsync", "status=progress"])
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


def open_serial(serial_port):
    ser = serial.Serial(port=serial_port, baudrate=SERIAL_BAUD, timeout=0.5)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print(f"[*] Serial opened: {serial_port} @ {SERIAL_BAUD}")
    drain_serial_input(ser, "pre-power")
    return ser


def drain_serial_input(ser, label, quiet_seconds=0.75, max_seconds=5.0):
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


# ---------------------------------------------------------------------------
# HDMI capture (identical to flash_test.py: warmed mjpeg -> numbered PNGs).
# ---------------------------------------------------------------------------
def start_hdmi_capture_stream():
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
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-y",
           "-f", "v4l2", "-input_format", "mjpeg",
           "-video_size", HDMI_CAPTURE_SIZE, "-framerate", HDMI_CAPTURE_RATE,
           "-i", HDMI_CAPTURE_DEVICE,
           "-vf", f"fps={HDMI_CAPTURE_SNAPSHOT_FPS}",
           os.path.join(HDMI_CAPTURE_FRAMES_DIR, "frame_%05d.png")]
    try:
        proc = subprocess.Popen(cmd, cwd=BUILD_DIR, stdin=subprocess.DEVNULL,
                                stdout=log_fd, stderr=subprocess.STDOUT)
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


def stop_hdmi_capture_stream(session):
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


def discover_devices():
    relay_hidraw = find_hidraw_device()
    serial_port = find_serial_port()
    storage_vid = STORAGE_VID
    storage_pid = STORAGE_PID
    storage_dev = find_usb_storage_by_vidpid(storage_vid, storage_pid)
    if not storage_vid or not storage_pid:
        candidates = [c for c in list_usb_storage_candidates()
                      if c[1] != "unknown" and c[2] != "unknown"]
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


def monitor_serial(ser, monitor_seconds):
    """Monitor serial for monitor_seconds, mirroring to stdout and a log file.

    Unlike flash_test.py, this does NOT bail on serial silence (the desktop is
    quiet once settled).  It keeps reading the whole window so the compositor's
    steady-state present timestamps are captured.  If a KDBG/bugcheck prompt
    appears it still grabs a concise dump.
    """
    print(f"[*] Monitoring serial for {monitor_seconds}s (no stall bail)")
    print(f"[*] Log: {LOG_FILE}")
    start = time.monotonic()
    recent_text = ""
    boot_menu_enter_sent = False
    kdbg_seen = False
    try:
        with open(LOG_FILE, "wb") as log_fd:
            while time.monotonic() - start < monitor_seconds:
                try:
                    data = ser.read(ser.in_waiting or 1)
                except (serial.SerialException, OSError) as e:
                    print(f"[!] serial read failed: {e}", file=sys.stderr)
                    return "serial-error"
                if not data:
                    continue
                log_fd.write(data)
                log_fd.flush()
                text = data.decode("utf-8", errors="replace")
                clean = text.encode("ascii", errors="replace").decode("ascii")
                sys.stdout.write(clean)
                sys.stdout.flush()
                recent_text = (recent_text + clean)[-KDBG_PROMPT_WINDOW:]
                if not boot_menu_enter_sent and BOOT_MENU_PROMPT in recent_text:
                    boot_menu_enter_sent = True
                    print("\n[*] Boot menu prompt detected; sending enter")
                    try:
                        ser.write(b"\r")
                        ser.flush()
                    except (serial.SerialException, OSError):
                        return "serial-error"
                if not kdbg_seen and (KDBG_PAGER_PROMPT in recent_text or KDBG_COMMAND_PROMPT in recent_text):
                    kdbg_seen = True
                    print("\n[!] KDBG prompt detected -- capturing dump (possible bugcheck)")
                    try:
                        ser.write(b"q")
                        ser.flush()
                        time.sleep(1.0)
                        for c in ("regs\r", "bt\r"):
                            ser.write(c.encode("ascii"))
                            ser.flush()
                            time.sleep(0.3)
                    except (serial.SerialException, OSError):
                        pass
                    # keep draining a bit so the dump lands in the log
                    drain_deadline = time.monotonic() + KDBG_DUMP_CAPTURE_SECONDS
                    while time.monotonic() < drain_deadline:
                        try:
                            d = ser.read(ser.in_waiting or 1)
                        except (serial.SerialException, OSError):
                            break
                        if d:
                            log_fd.write(d)
                            log_fd.flush()
                            sys.stdout.write(d.decode("utf-8", errors="replace").encode("ascii", errors="replace").decode("ascii"))
                            sys.stdout.flush()
                    return "kdbg-dump"
        print(f"\n[*] Monitor window ({monitor_seconds}s) complete")
        return "monitored"
    except KeyboardInterrupt:
        print("\n[!] Interrupted")
        return "interrupted"
    finally:
        ser.close()
        os.system("stty sane 2>/dev/null")


def run():
    if not os.path.exists(REACTOS_IMG):
        print(f"[!] {REACTOS_IMG} not found. Build it first (atomically), e.g.:", file=sys.stderr)
        print("    RPI5DOD_ROOT=1 ../scripts/wddm_reapply_wiring.sh && cmake . && ninja livecd && ninja reactosimg", file=sys.stderr)
        sys.exit(1)

    monitor_seconds = DEFAULT_MONITOR_SECONDS
    if len(sys.argv) > 1:
        try:
            monitor_seconds = int(sys.argv[1])
        except ValueError:
            print(f"[!] invalid monitor_seconds '{sys.argv[1]}'", file=sys.stderr)
            sys.exit(1)

    img_size = os.path.getsize(REACTOS_IMG)
    img_mtime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(REACTOS_IMG)))
    print(f"[*] NO-REBUILD flash harness")
    print(f"[*] Image: {REACTOS_IMG} ({img_size} bytes, built {img_mtime})")
    print(f"[*] Monitor window: {monitor_seconds}s")

    devices = discover_devices()
    relays_all_off(devices)
    storage_to_host(devices)

    if devices.storage_vid and devices.storage_pid:
        devices.storage_dev = wait_for_usb_storage_by_vidpid(devices.storage_vid, devices.storage_pid)
    else:
        devices.storage_dev, devices.storage_vid, devices.storage_pid = wait_for_single_usb_storage()
    devices.storage_serial = usb_storage_serial(devices.storage_dev)
    if devices.storage_serial:
        print(f"[*] Flash target: {devices.storage_dev} {devices.storage_vid}:{devices.storage_pid} serial={devices.storage_serial}")
    flash(devices.storage_dev)
    time.sleep(1)

    hdmi_capture = None
    try:
        ser = open_serial(devices.serial_port)
        ser.reset_input_buffer()
        hdmi_capture = start_hdmi_capture_stream()
        storage_to_pi(devices)
        result = monitor_serial(ser, monitor_seconds)
        print(f"[*] Boot result: {result}")
    finally:
        stop_hdmi_capture_stream(hdmi_capture)
        storage_to_host(devices)
    print("[*] Ready for next session")


def main():
    with HardwareQueue():
        run()


if __name__ == "__main__":
    main()
