#!/usr/bin/python3
#
# Copyright © 2024 Igalia S.L.
# SPDX-License-Identifier: MIT

# Usage:
#   u_trace_gather.py gather_all \
#    --loops 1 --launcher "renderdoccmd replay --loops 12" \
#    --traces-list /path/to/traces.txt \
#    --traces-dir /path/to/dir/with/traces/ \
#    --results /path/to/results/ \
#    --alias new-shiny-opt
#
# Where traces.txt:
#   trace1.rdc
#   ; trace2.rdc // Disabled
#   trace3.rdc

from dataclasses import dataclass
from enum import Enum
import os

import argparse
import re
import subprocess

from types import SimpleNamespace


class RunResultStatus(Enum):
    SUCCESS = 0,
    FAILURE = 1,
    RETRY = 2


@dataclass
class RunResult:
    status: RunResultStatus
    description: str


class DmesgFailureFinder():
    DMESG_COMMAND = ['dmesg', '--level', 'emerg,alert,crit,err,warn,notice']

    def __init__(self, regex) -> None:
        self.regex = regex
        self._new_messages = []
        self._last_message = None

        self.update_dmesg()

    def get_workload_result(self):
        self.update_dmesg()

        if self._new_messages:
            for line in self._new_messages:
                if self.regex.search(line):
                    return RunResult(RunResultStatus.FAILURE, line)

        return RunResult(RunResultStatus.SUCCESS, "")

    def update_dmesg(self):
        dmesg = subprocess.check_output(self.DMESG_COMMAND).decode('utf-8')
        dmesg = dmesg.strip().splitlines()

        l = 0
        for index, item in enumerate(reversed(dmesg)):
            if item == self._last_message:
                l = len(dmesg) - index  # don't include the matched element
                break
        self._new_messages = dmesg[l:]

        # Attempt to store the last element of dmesg, unless there was no dmesg
        self._last_message = dmesg[-1] if dmesg else None
        pass


def gather(args) -> None:
    results_dir = f"{args.results}/{args.name}/"
    os.makedirs(results_dir, exist_ok=True)

    failure_finder = DmesgFailureFinder(re.compile("gpu fault"))

    for loop in range(args.loops):
        print(f"Start of loop {loop}")

        output_file = f"{results_dir}/trace_{args.name}_{args.alias}_{loop}.csv"
        output_log = f"{results_dir}/{args.name}_{args.alias}_{loop}.log"

        env = os.environ.copy()
        env["MESA_VK_ABORT_ON_DEVICE_LOSS"] = "1"
        env["MESA_GPU_TRACEFILE"] = output_file
        env["MESA_GPU_TRACES"] = "print_csv"

        print(f"{args.command}")

        with open(output_log, 'w') as file:
            ret = subprocess.run(
                args.command,
                stdout=file,
                stderr=subprocess.STDOUT,
                stdin=subprocess.PIPE,
                env=env,
                shell=False,
            )

        result = failure_finder.get_workload_result()

        if ret.returncode != 0:
            print(f"\tCommand exited with code {ret}")

        if result.status != RunResultStatus.SUCCESS:
            print(f"GPU failure: \"{result.description}\"")

        if ret.returncode != 0 or result.status != RunResultStatus.SUCCESS:
            try:
                os.remove(output_file)
            except OSError:
                pass

            break


def gather_all(args) -> None:
    trace_files = []
    with open(args.traces_list) as file:
        for line in file:
            if len(line) > 0 and line[0].isalnum():
                trace_files.append(line.rstrip())

    for i, trace_file in enumerate(trace_files):
        full_path = f"{args.traces_dir}/{trace_file}"
        print(f"Evaluating [{i + 1}/{len(trace_files)}] {trace_file} (\"{full_path}\")")

        gather_args = SimpleNamespace()
        gather_args.loops = args.loops
        gather_args.command = f"{args.launcher} {full_path}".split(" ")
        gather_args.results = args.results
        gather_args.name = trace_file
        gather_args.alias = args.alias

        gather(gather_args)

    print("Done.")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers()

    gather_all_args = sub.add_parser('gather_all', help='')
    gather_all_args.add_argument('--loops', type=int, required=True,
                                 help="How many times the command would be repeated in order to average the results.")
    gather_all_args.add_argument('--traces-list', type=str, required=True, help="File with a list of trace files.")
    gather_all_args.add_argument('--traces-dir', type=str, required=True,
                                 help="Path to the directory with trace files.")
    gather_all_args.add_argument('--results', type=str, required=True, help="Folder where to write results.")
    gather_all_args.add_argument('--alias', type=str, required=True, help="Alias for the change being tested.")
    gather_all_args.add_argument('--launcher', type=str, required=True,
                                 help="Launcher that accepts trace file as an argument.")
    gather_all_args.set_defaults(func=gather_all)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
