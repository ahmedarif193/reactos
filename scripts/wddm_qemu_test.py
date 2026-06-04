#!/usr/bin/env python3
"""
WDDM/DWM ARM64 bring-up test harness.

Boots output-Clang-arm64-debug/livecd.iso on qemu-system-aarch64 (virt) WITH a
matching PCI display device (bochs-display = PCI VEN_1234&DEV_1111) so the
softgpu / kmdod WDDM miniport can PnP-bind via the CriticalDeviceDatabase entry.

Captures:
  * serial log  -> /tmp/wddm_serial.log
  * screenshot  -> /tmp/wddm_screen.ppm  (via QMP screendump)

Usage:
  python3 wddm_qemu_test.py [seconds] [display_device]
    seconds        : how long to let it boot before screenshot (default 90)
    display_device : bochs-display | VGA | ramfb | virtio-gpu-pci (default bochs-display)
"""
import os, sys, json, socket, subprocess, time, signal

BUILD_DIR = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", "output-Clang-arm64-debug"))
ISO = os.path.join(BUILD_DIR, "livecd.iso")
EFI = "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
SERIAL_LOG = "/tmp/wddm_serial.log"
SCREEN = "/tmp/wddm_screen.ppm"
QMP_SOCK = "/tmp/wddm_qmp.sock"

def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 90
    disp = sys.argv[2] if len(sys.argv) > 2 else "bochs-display"

    for p in (SERIAL_LOG, SCREEN, QMP_SOCK):
        try: os.remove(p)
        except OSError: pass

    # bochs-display / VGA / virtio-gpu-pci attach to the virt PCIe root.
    # ramfb is a System bus device (the default). We add a SECOND adapter so
    # PnP sees PCI\VEN_1234&DEV_1111 and binds softgpu via the CDDB.
    # Keep ramfb (UEFI GOP -> framebuf miniport drives the boot desktop) AND
    # add the extra PCI display adapter that softgpu/kmdod PnP-match.
    if disp == "ramfb":
        disp_args = ["-device", "ramfb"]
    else:
        disp_args = ["-device", "ramfb", "-device", disp]

    cmd = [
        "qemu-system-aarch64",
        "-smp", "4",
        "-machine", "virt,gic-version=3",
        "-cpu", "cortex-a57",
        "-m", "24G",
        "-bios", EFI,
        *disp_args,
        "-drive", f"if=virtio,media=cdrom,readonly=on,file={ISO}",
        "-boot", "order=d,menu=on",
        "-display", "none",
        "-vnc", "none",                       # headless VGA target QMP can screendump
        "-serial", f"file:{SERIAL_LOG}",
        "-qmp", f"unix:{QMP_SOCK},server,nowait",
    ]
    print("WDDM QEMU test")
    print("  display device:", disp)
    print("  iso          :", ISO)
    print("  serial log   :", SERIAL_LOG)
    print("  screenshot   :", SCREEN)
    print("  duration     :", secs, "s")
    print("  cmd:", " ".join(cmd))

    proc = subprocess.Popen(cmd, cwd=BUILD_DIR,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL)
    print("QEMU PID", proc.pid)
    try:
        # Wait for the boot window, then take a screenshot via QMP.
        time.sleep(secs)
        ok = qmp_screendump()
        if not ok:
            print("WARN: QMP screendump failed")
        # let serial flush a touch more
        time.sleep(2)
    finally:
        try:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=10)
        except Exception:
            proc.kill()
    print("done. serial:", SERIAL_LOG, "screen:", SCREEN)

def qmp_recv(f):
    line = f.readline()
    if not line:
        return None
    return json.loads(line)

def qmp_screendump():
    if not os.path.exists(QMP_SOCK):
        print("QMP socket missing")
        return False
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(QMP_SOCK)
        f = s.makefile("rw")
        qmp_recv(f)                                   # greeting
        f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n"); f.flush()
        qmp_recv(f)
        f.write(json.dumps({"execute": "screendump",
                            "arguments": {"filename": SCREEN}}) + "\n"); f.flush()
        r = qmp_recv(f)
        s.close()
        print("screendump reply:", r)
        return os.path.exists(SCREEN) and os.path.getsize(SCREEN) > 0
    except Exception as e:
        print("QMP error:", e)
        return False

if __name__ == "__main__":
    main()
