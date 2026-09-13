#!/usr/bin/python3
#
# Copyright © 2024 Igalia S.L.
# SPDX-License-Identifier: MIT

# Usage:
#   u_trace_compare.py compare \
#    --results /path/to/results/ \
#    --loops-merged true \
#    --alias-a default \
#    --alias-b new-shiny-opt \
#    --event-start start_render_pass \
#    --event-end end_render_pass \
#    --filter "int(params['drawCount']) > 10"
#
# Note set --loops-merged when u_trace_gather.py was run with --loops 1
# and frame looping was done in the target application.
#
# To find where in the frame the event takes place, it could be searched
# by its index in the output of:
#   u_trace_compare.py details \
#    --results /path/to/results/ \
#    --trace-name test.rdc \
#    --alias default \
#    --event-start start_render_pass \
#    --event-end end_render_pass
#


import argparse
from collections import namedtuple
from dataclasses import dataclass
import os
import statistics
import csv

@dataclass
class GPUEventAggregate:
    idx: int
    measurements: list[int]
    workload_name: str
    event_name: str
    description: dict

    def __str__(self):
        return (
            f"[{self.workload_name}] {self.event_name} [{self.idx}] \n"
            f"\tmeasurement_0: {self.measurements[0]}\tdesc: {self.description}\n"
        )


@dataclass
class GPUEventDiff:
    event_idx: int

    mean_diff: int
    mean_diff_pct: float

    ks_statistic: float
    pvalue: float
    result_significant: bool

    workload_name: str
    event_name: str
    description_a: dict
    description_b: dict
    description_diff_a: dict
    description_diff_b: dict

    alias_a: str
    alias_b: str

    measurements_a: list[int]
    measurements_b: list[int]

    def __post_init__(self):
        for key in list(self.description_a.keys()):
            if self.description_a[key] != self.description_b[key]:
                self.description_diff_a[key] = self.description_a[key]
                self.description_diff_b[key] = self.description_b[key]
                # del self.description_a[key]

    def __str__(self):
        result = (
            f"[{self.workload_name}] {self.event_name} [{self.event_idx}] "
            f"{'Δ' if self.mean_diff_pct < 0 else '∇'} {self.mean_diff_pct:.1f}% "
            f"ks: {self.ks_statistic} pval: {self.pvalue:.4f}\n"
            f"\tmeasurements_{self.alias_a}: {*self.measurements_a,}\n"
            f"\tmeasurements_{self.alias_b}: {*self.measurements_b,}\n"
            f"\t{self.description_a}\n"
        )
        if self.description_diff_a:
            result += f"\t{self.alias_a}: {self.description_diff_a}\n"
        if self.description_diff_b:
            result += f"\t{self.alias_b}: {self.description_diff_b}\n"

        return result


def parse_with_alias(args, workload_name: str, alias: str) -> list[GPUEventAggregate]:
    ParsedEvent = namedtuple('ParsedEvent', ['timestamp', 'name', 'params'])
    events_aggregate: list[GPUEventAggregate] = []
    loop_idx = 0
    while True:
        frames = [[]]
        last_frame = 0

        file_name = f"{args.results}/{workload_name}/trace_{workload_name}_{alias}_{loop_idx}.csv"
        try:
            with open(file_name, 'r') as input_csv:
                reader = csv.reader(input_csv, delimiter=',', skipinitialspace=True)
                for row in reader:
                    if len(row) < 4 or not row[0].isdigit():
                        continue
                    frame = int(row[0])
                    if frame != last_frame:
                        frames.append([])
                        last_frame = frame
                    timestamp = row[2]
                    event_name = row[3]
                    params = {}
                    for param in row[4:]:
                        try:
                            key, value = param.split('=', 1)
                        except ValueError:
                            break
                        params[key] = value

                    frames[-1].append(ParsedEvent(timestamp, event_name, params))

        except FileNotFoundError:
            if loop_idx == 0:
                print(f"\tZero results found for workload '{workload_name}' and alias '{alias}' (\"{file_name}\")")
            break

        if args.loops_merged:
            del frames[0]
            # The last frame could be partially lost
            del frames[-1]

        event_idx = 0
        for target_frame in reversed(frames):
            if args.loops_merged:
                # We compare frames from a single run
                event_idx = 0

            start_time_ns = 0
            new = False
            for event in target_frame:
                if event.name == args.event_start:
                    # Events should be created only once
                    if event_idx == len(events_aggregate):
                        agg = GPUEventAggregate(event_idx, [], workload_name, event.name, event.params)
                        events_aggregate.append(agg)
                        new = True
                    start_time_ns = int(event.timestamp)
                if event.name == args.event_end:
                    agg = events_aggregate[event_idx]
                    if new:
                        agg.description = agg.description | event.params
                    new = False

                    duration = int(event.timestamp) - start_time_ns
                    agg.measurements.append(duration)

                    event_idx += 1

        if args.loops_merged:
            break

        loop_idx += 1

    assert (len(events_aggregate) != 0)

    if args.filter_func:
        events_aggregate = list(filter(lambda e: args.filter_func(e.description), events_aggregate))

    return events_aggregate


def parse_all_with_alias(args, alias: str) -> list[GPUEventAggregate]:
    events_aggregate: list[GPUEventAggregate] = []

    for dir_name in os.listdir(args.results):
        f = os.path.join(args.results, dir_name)
        if os.path.isdir(f):
            try:
                events_aggregate.extend(parse_with_alias(args, dir_name, alias))
            except Exception as ex:
                print(f"Invalid csv for '{dir_name}'/'{alias}'")
                raise ex

    return events_aggregate


def print_diffs_plain(args, diffs: list[GPUEventDiff]) -> None:
    diffs.sort(key=lambda x: x.mean_diff_pct)

    # TODO: configurable threshold?
    helped = list(filter(lambda d: d.result_significant and d.mean_diff_pct < -0.5, diffs))
    hurt = list(filter(lambda d: d.result_significant and d.mean_diff_pct > 0.5, diffs))

    helped_total_a = sum(statistics.mean(d.measurements_a) for d in helped)
    helped_total_b = sum(statistics.mean(d.measurements_b) for d in helped)
    if helped_total_a > 0:
        win_in_helped_pct = (helped_total_b - helped_total_a) / helped_total_a * 100.0
    else:
        win_in_helped_pct = 0

    hurt_total_a = sum(statistics.mean(d.measurements_a) for d in hurt)
    hurt_total_b = sum(statistics.mean(d.measurements_b) for d in hurt)
    if hurt_total_a > 0:
        loss_in_hurt_pct = (hurt_total_b - hurt_total_a) / hurt_total_a * 100.0
    else:
        loss_in_hurt_pct = 0

    total_a = sum(statistics.mean(d.measurements_a) for d in diffs)
    total_b = sum(statistics.mean(d.measurements_b) for d in diffs)
    total_win_pct = (total_b - total_a) / total_a * 100.0

    print("ALL:")
    for diff in diffs:
        print(diff)

    print()
    print("HELPED:")
    for diff in helped:
        print(diff)

    print()
    print("HURT:")
    for diff in hurt:
        print(diff)

    print(f"TOTAL: {len(diffs)} {'Δ' if total_win_pct < 0 else '∇'} {total_win_pct:.5f}%")
    print(
        f"TOTAL HELPED: {len(helped)} Δ {win_in_helped_pct:.1f}% (where '{args.alias_b}' is faster than '{args.alias_a}')")
    print(f"TOTAL HURT: {len(hurt)} ∇ {loss_in_hurt_pct:.1f}% (where '{args.alias_b}' is slower than '{args.alias_a}')")
    print(f"FILTER: {args.filter}")


def print_diffs_csv(args, diffs: list[GPUEventDiff]) -> None:
    description_a_keys = []
    description_diff_keys = []
    for diff in diffs:
        for key in diff.description_a:
            if key not in description_a_keys:
                description_a_keys.append(key)
        for key in diff.description_diff_a:
            if key not in description_diff_keys:
                description_diff_keys.append(key)

    with open(args.csv, 'w', newline='') as csvfile:
        field_names = ['workload', 'event_idx', 'event_name', f'mean_{args.alias_a}', f'mean_{args.alias_b}',
                       'mean_diff', 'ks_statistic', 'pvalue']
        field_names.extend(description_a_keys)
        for key in description_diff_keys:
            field_names.append(f"{key}_{args.alias_a}")
            field_names.append(f"{key}_{args.alias_b}")

        csv_writer = csv.writer(csvfile, delimiter='\t', dialect='unix')
        csv_writer.writerow(field_names)
        for diff in diffs:
            mean_a = int(statistics.mean(diff.measurements_a))
            mean_b = int(statistics.mean(diff.measurements_b))
            row = [diff.workload_name, diff.event_idx, diff.event_name, mean_a, mean_b,
                   int(diff.mean_diff), diff.ks_statistic, round(diff.pvalue, 4)]
            # Add description_a values
            for key in description_a_keys:
                row.append(diff.description_a.get(key, ''))

            # Add description_diff values
            for key in description_diff_keys:
                row.append(diff.description_diff_a.get(key, ''))
                row.append(diff.description_diff_b.get(key, ''))

            csv_writer.writerow(row)


def compare(args) -> None:
    from scipy.stats import ks_2samp

    events_a = parse_all_with_alias(args, args.alias_a)
    events_b = parse_all_with_alias(args, args.alias_b)

    assert (len(events_a) == len(events_b))

    diffs: list[GPUEventDiff] = []
    for event_a, event_b in zip(events_a, events_b):
        # GPU cannot randomly become much faster, only slower, so as
        # a trivial way to combat uncertainty - remove only the longest measurement.
        # DISABLED for now since it makes more difficult to find renderpass
        # event_a.measurements.remove(max(event_a.measurements))
        # event_b.measurements.remove(max(event_b.measurements))

        mean_a = statistics.mean(event_a.measurements)
        mean_b = statistics.mean(event_b.measurements)

        # We don't expect enough measurements to apply statistical methods
        # suitable for normal distributions.
        # Kolmogorov–Smirnov test gives us a simple metric to understand
        # whether measurements belong to different distributions,
        # if yes - we could meaningfully compare them.
        ks_stats = ks_2samp(event_a.measurements, event_b.measurements)

        diff = GPUEventDiff(
            event_idx=event_a.idx,
            mean_diff=mean_b - mean_a,
            mean_diff_pct=(mean_b - mean_a) / mean_a * 100.0,
            workload_name=event_a.workload_name,
            event_name=event_a.event_name,
            description_a=event_a.description,
            description_b=event_b.description,
            description_diff_a=dict(),
            description_diff_b=dict(),
            alias_a=args.alias_a,
            alias_b=args.alias_b,
            ks_statistic=ks_stats.statistic,
            pvalue=ks_stats.pvalue,
            # TODO: This threshold is a random guess, is it good enough?
            result_significant=ks_stats.pvalue < 0.10 and ks_stats.statistic > 0.8,
            measurements_a=event_a.measurements,
            measurements_b=event_b.measurements,
        )

        diffs.append(diff)

    if args.csv:
        print_diffs_csv(args, diffs)
    else:
        print_diffs_plain(args, diffs)


def details(args) -> None:
    args.loops_merged = True
    events = parse_with_alias(args, args.trace_name, args.alias)

    for event in events:
        print(event)


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers()

    compare_args = sub.add_parser('compare', help='Compare two results.')
    compare_args.add_argument('--results', type=str, required=True, help="Folder with results.")
    compare_args.add_argument('--loops-merged', type=bool, required=True,
                              help="true if single trace contains multiple loops for analysis.")
    compare_args.add_argument('--alias-a', type=str, required=True, help="")
    compare_args.add_argument('--alias-b', type=str, required=True, help="")
    compare_args.add_argument('--event-start', type=str, required=True, help="E.g. start_render_pass")
    compare_args.add_argument('--event-end', type=str, required=True, help="E.g. end_render_pass")
    compare_args.add_argument('--csv', type=str, required=False, help="CSV file to write the output.")
    compare_args.add_argument('--filter', type=str, required=False, help="Lambda function for filtering events.")
    compare_args.set_defaults(func=compare)

    info_args = sub.add_parser('details', help='Get info about tracepoints in a specific results log.')
    info_args.add_argument('--results', type=str, required=True, help="Folder with results.")
    info_args.add_argument('--trace-name', type=str, required=True, help="Name of the trace, e.g. test.json")
    info_args.add_argument('--alias', type=str, required=True, help="")
    info_args.add_argument('--event-start', type=str, required=True, help="E.g. start_render_pass")
    info_args.add_argument('--event-end', type=str, required=True, help="E.g. end_render_pass")
    info_args.set_defaults(func=details)

    args = parser.parse_args()

    args.filter_func = None
    if hasattr(args, 'filter') and args.filter:
        args.filter_func = eval(f"lambda params: {args.filter}")

    args.func(args)


if __name__ == "__main__":
    main()
