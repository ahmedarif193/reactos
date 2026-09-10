#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Ahmed Arif
"""Click the exact requested visible pin verb, recording screenshot and OCR.

This helper never invokes private shell APIs or edits the pin folder. The guest
probe independently checks the resulting set and shortcut bytes. It only opens
the explicitly supplied disposable VM's QEMU monitor socket.
"""
import argparse
import json
import re
import socket
import subprocess
import time
from pathlib import Path


def qmp(monitor, command, arguments=None):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(5)
        connection.connect(str(monitor))
        stream = connection.makefile("rb")
        json.loads(stream.readline())
        for request in ({"execute": "qmp_capabilities"}, {"execute": command, "arguments": arguments or {}}):
            connection.sendall(json.dumps(request).encode() + b"\n")
            while True:
                response = json.loads(stream.readline())
                if "error" in response:
                    raise RuntimeError(response)
                if "return" in response:
                    break
        return response["return"]


def hmp(monitor, command):
    return qmp(monitor, "human-monitor-command", {"command-line": command})


def move(monitor, x, y):
    qmp(monitor, "input-send-event", {"events": [{"type": "abs", "data": {"axis": axis, "value": round(value * 32767)}} for axis, value in (("x", x), ("y", y))]})


def click(monitor, x, y, button=1):
    move(monitor, x, y)
    time.sleep(0.1)
    for down in (True, False):
        qmp(monitor, "input-send-event", {"events": [{"type": "btn", "data": {"button": "left" if button == 1 else "right", "down": down}}]})
        time.sleep(0.1)


def select_menu(monitor, ocr, output, action):
    output.mkdir(parents=True, exist_ok=True)
    for attempt in range(15):
        shot = output / f"menu-{attempt:02d}.ppm"
        png = shot.with_suffix(".png")
        hmp(monitor, f"screendump {shot}")
        subprocess.run(["sips", "-s", "format", "png", str(shot), "--out", str(png)], check=True, capture_output=True)
        shot.unlink()
        rows = json.loads(subprocess.check_output([str(ocr), str(png)]))
        png.with_suffix(".json").write_text(json.dumps(rows, indent=2) + "\n")
        pattern = r"\bUnpin\b.*taskbar" if action == "UNPIN" else r"\bPin\b.*taskbar"
        matches = [row for row in rows if re.search(pattern, row["text"], re.I)]
        if len(matches) == 1:
            row = matches[0]
            print(f"UI_CLICK {action}: {row}", flush=True)
            click(monitor, row["x"], row["y"])
            return
        if len(matches) > 1:
            raise RuntimeError(f"Ambiguous menu matches: {matches}")
        more = [row for row in rows if "show more options" in row["text"].lower()]
        if len(more) == 1:
            click(monitor, more[0]["x"], more[0]["y"])
        time.sleep(0.6)
    raise RuntimeError(f"No visible {action} taskbar command; inspect {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("monitor", type=Path)
    parser.add_argument("ocr", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("action", choices=["PIN", "UNPIN"])
    args = parser.parse_args()
    select_menu(args.monitor, args.ocr, args.output, args.action)


if __name__ == "__main__":
    main()
