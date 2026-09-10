#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Ahmed Arif
"""Follow the guest's boot-suite markers through its visible shell UI."""
import argparse
import importlib.util
import json
import re
import struct
import subprocess
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location("pin_menu", Path(__file__).with_name("drive-menu.py"))
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)


def screenshot(args, name):
    image = args.output / (name + ".png")
    raw = image.with_suffix(".ppm")
    ui.hmp(args.monitor, f"screendump {raw}")
    subprocess.run(["sips", "-s", "format", "png", str(raw), "--out", str(image)], check=True, capture_output=True)
    raw.unlink()
    return image


def find_button(args, app, cache):
    image = screenshot(args, "taskbar")
    width, height = struct.unpack(">II", image.read_bytes()[16:24])
    y = (height - 22) / height
    candidates = ([cache[app]] if app in cache else []) + [(pixel / width, y) for pixel in range(20, width - 145, 22)]
    for i, (x, y) in enumerate(candidates):
        ui.move(args.monitor, x, y)
        time.sleep(0.9)
        image = screenshot(args, f"hover-{app}-{i:02d}")
        rows = json.loads(subprocess.check_output([str(args.ocr), str(image)]))
        for row in rows:
            match = re.search(r"Pin\s*Test\s*([A-E])\b", row["text"], re.I)
            if row["y"] > 0.6 and match:
                found = match[1].upper()
                cache[found] = (x, y)
                if found == app:
                    print(f"UI_BUTTON app={app} x={x:.4f} y={y:.4f} text={row['text']!r}", flush=True)
                    return x, y
    raise RuntimeError(f"No visible taskbar tooltip for app {app}; inspect {args.output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("monitor", type=Path)
    parser.add_argument("log", type=Path)
    parser.add_argument("ocr", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    seen = 0
    events = 0
    cache = {}
    started = time.monotonic()
    while time.monotonic() - started < args.timeout:
        lines = args.log.read_text(errors="replace").splitlines() if args.log.exists() else []
        for line in lines[seen:]:
            if "PIN_" not in line:
                continue
            print(line, flush=True)
            events += 1
            match = re.search(r"PIN_UI_READY step=(\d+) action=(PIN|UNPIN) app=([A-E])", line)
            if match:
                ui.select_menu(args.monitor, args.ocr, args.output / f"event-{events:03d}-{match[2]}-{match[3]}", match[2])
                cache.clear()
            match = re.search(r"PIN_LAUNCH_READY step=(\d+) app=([A-E]) windows=(\d+)", line)
            if match:
                x, y = find_button(args, match[2], cache)
                second = int(match[3]) > 1
                if second:
                    ui.qmp(args.monitor, "input-send-event", {"events": [{"type": "key", "data": {"key": {"type": "qcode", "data": "shift"}, "down": True}}]})
                ui.click(args.monitor, x, y)
                if second:
                    ui.qmp(args.monitor, "input-send-event", {"events": [{"type": "key", "data": {"key": {"type": "qcode", "data": "shift"}, "down": False}}]})
            match = re.search(r"PIN_TASKBAR_UNPIN_READY step=(\d+) app=([A-E])", line)
            if match:
                x, y = find_button(args, match[2], cache)
                ui.click(args.monitor, x, y, button=2)
                time.sleep(0.5)
                ui.select_menu(args.monitor, args.ocr, args.output / f"event-{events:03d}-taskbar-unpin", "UNPIN")
                cache.clear()
            if any(marker in line for marker in ("PIN_SET_OK", "PIN_CHECKPOINT_FIVE_READY", "PIN_EXPLORER_PERSISTENCE_OK", "PIN_REBOOT_PERSISTENCE_OK", "PIN_UNPIN_RUNNING_OK")):
                screenshot(args, f"event-{events:03d}-state")
            if "PIN_REBOOT_PERSISTENCE_OK" in line:
                cache.clear()
            if "PIN_REBOOT_REQUESTED" in line or "PIN_REBOOT_LAUNCHES_OK" in line:
                order = sorted("CEBAD", key=lambda app: cache[app][0])
                if order != list("CEBAD"):
                    raise RuntimeError(f"Taskbar pin order changed: {order}, locations={cache}")
                print(f"PIN_UI_ORDER_OK order={''.join(order)} positions={cache}", flush=True)
            if "PIN_PARITY_DONE" in line:
                if not re.search(r"phase=2 .*failures=0\b", line):
                    raise RuntimeError("Guest test did not finish successfully")
                print("PIN_UI_DRIVER_DONE", flush=True)
                return
        seen = len(lines)
        time.sleep(0.15)
    raise TimeoutError("Guest did not emit a terminal result")


if __name__ == "__main__":
    main()
