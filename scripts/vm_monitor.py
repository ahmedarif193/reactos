#!/usr/bin/env python3
"""Forwarder: the real vm_monitor.py lives outside the tree.

The monitor, its firmware blob and the app-disk tooling were moved to
/Users/mac/reactos-scripts so that harness changes stop churning this repo.
Override the location with ROS_VM_MONITOR.

    ../scripts/vm_monitor.py --img --smp 4
    ~/reactos-scripts/vm_monitor.py --img --smp 4      (same thing)
"""

import os
import sys

TARGET = os.environ.get(
    "ROS_VM_MONITOR",
    os.path.expanduser("~/reactos-scripts/vm_monitor.py"),
)

if not os.path.isfile(TARGET):
    sys.exit(f"vm_monitor.py not found: {TARGET} (set ROS_VM_MONITOR)")

os.execv(sys.executable, [sys.executable, TARGET] + sys.argv[1:])
