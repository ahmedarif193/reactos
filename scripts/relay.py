#!/usr/bin/env python3
"""Control a 5131:2007 USB HID 4-channel relay board."""

import argparse
import os
import sys

VENDOR_ID = "5131"
PRODUCT_ID = "2007"


def find_hidraw(vid: str, pid: str) -> str | None:
    """Find /dev/hidrawN matching the given USB VID:PID."""
    for entry in os.listdir("/sys/class/hidraw"):
        uevent_path = f"/sys/class/hidraw/{entry}/device/uevent"
        try:
            with open(uevent_path) as f:
                for line in f:
                    if line.startswith("HID_ID="):
                        # Format: HID_ID=0003:00005131:00002007
                        parts = line.strip().split(":")
                        if len(parts) == 3:
                            dev_vid = parts[1].lstrip("0") or "0"
                            dev_pid = parts[2].lstrip("0") or "0"
                            if dev_vid.lower() == vid.lower() and dev_pid.lower() == pid.lower():
                                return f"/dev/{entry}"
        except OSError:
            continue
    return None


def relay_cmd(pin: int, state: int) -> bytes:
    header = 0xA0
    checksum = (header + pin + state) & 0xFF
    return bytes([header, pin, state, checksum])


def main():
    parser = argparse.ArgumentParser(description="USB HID relay control (5131:2007)")
    parser.add_argument("pin", type=int, choices=range(1, 5), help="Relay pin (1-4)")
    parser.add_argument("value", type=int, choices=[0, 1], help="0=OFF, 1=ON")
    parser.add_argument("-d", "--device", default=None, help="hidraw device path (auto-detected if omitted)")
    args = parser.parse_args()

    device = args.device or find_hidraw(VENDOR_ID, PRODUCT_ID)
    if not device:
        print(f"Relay board {VENDOR_ID}:{PRODUCT_ID} not found", file=sys.stderr)
        sys.exit(1)

    cmd = relay_cmd(args.pin, args.value)
    try:
        with open(device, "wb") as f:
            f.write(cmd)
    except PermissionError:
        print(f"Permission denied: {device}", file=sys.stderr)
        print("Run: sudo chmod 666 " + device, file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print(f"Device not found: {device}", file=sys.stderr)
        sys.exit(1)

    state_str = "ON" if args.value else "OFF"
    print(f"Relay {args.pin} → {state_str}  [{cmd.hex(' ')}]")


if __name__ == "__main__":
    main()
