#!/usr/bin/env python3
# wgodot-changes::file
"""Summarize a log captured with --wgodot-startup-profile."""

import argparse
from collections import defaultdict
from pathlib import Path
import re


TIMING = re.compile(
    r"\[wgodot-startup\] thread=(\d+) start=([\d.]+) ms "
    r"total=([\d.]+) ms self=([\d.]+) ms (.*)"
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--limit", type=int, default=15)
    args = parser.parse_args()
    entries = []
    categories = defaultdict(lambda: [0, 0.0])
    complete = False
    for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
        if "First main-loop iteration finished; profiling disabled." in line:
            complete = True
        match = TIMING.search(line)
        if not match:
            continue
        thread, start, total, own, label = match.groups()
        entry = (float(own), float(total), float(start), thread, label.strip())
        entries.append(entry)
        operation = re.split(r" (?:res|uid|user|gdscript)://", label, maxsplit=1)[0].strip()
        categories[operation][0] += 1
        categories[operation][1] += entry[0]

    if not entries:
        parser.error("No startup timing entries found in this log.")
    elapsed = max(entry[2] + entry[1] for entry in entries)
    print(f"Profile {'complete' if complete else 'incomplete'}; last scope ends at {elapsed / 1000:.3f} s.")
    print("Self time excludes nested scopes; totals across threads can overlap.")
    print("\nSelf seconds   Calls   Operation")
    for operation, (count, own) in sorted(categories.items(), key=lambda item: item[1][1], reverse=True)[:args.limit]:
        print(f"{own / 1000:12.3f} {count:7d}   {operation}")
    print("\nSelf seconds   Total seconds   Start seconds   Thread   Scope")
    for own, total, start, thread, label in sorted(entries, reverse=True)[:args.limit]:
        print(f"{own / 1000:12.3f} {total / 1000:15.3f} {start / 1000:15.3f} {thread:>8}   {label}")


if __name__ == "__main__":
    main()
