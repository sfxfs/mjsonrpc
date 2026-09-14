#!/usr/bin/env python3
"""Compare mjsonrpc benchmark results against a stored baseline.

Reads two JSON files produced by `mjsonrpc-benchmark --json` and reports
speed / allocation regressions.  Exit code is 1 when a hard regression is
detected, 0 otherwise (including when no baseline exists yet).

Usage:
    compare_bench.py --baseline benchmark/baseline.json \
                     --current  benchmark-results.json \
                     --summary-md compare-summary.md
"""

from __future__ import annotations

import argparse
import json
import os
import sys


def load(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def fmt_ops(value: float) -> str:
    if value >= 1000:
        return f"{value:,.0f}"
    return f"{value:.1f}"


def pct(changed: float, base: float) -> str:
    if base <= 0:
        return "n/a"
    delta = (changed - base) / base * 100.0
    return f"{delta:+.1f}%"


def compare_meta(base: dict, cur: dict) -> list[str]:
    notes: list[str] = []
    bmeta = base.get("meta", {})
    cmeta = cur.get("meta", {})
    for key, label in (("cpu", "CPU"), ("arch", "architecture"),
                       ("platform", "platform"), ("build_type", "build type")):
        bval = bmeta.get(key)
        cval = cmeta.get(key)
        if bval and cval and bval != cval:
            notes.append(
                f"{label} differs from baseline (`{bval}` vs `{cval}`); "
                "numbers are only roughly comparable."
            )
    return notes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True,
                        help="baseline JSON (produced by mjsonrpc-benchmark)")
    parser.add_argument("--current", required=True,
                        help="current run JSON")
    parser.add_argument("--summary-md", default=None,
                        help="write a markdown comparison table to this file")
    parser.add_argument("--warn-pct", type=float, default=20.0,
                        help="speed drop %% that triggers a warning")
    parser.add_argument("--fail-pct", type=float, default=50.0,
                        help="speed drop %% that fails the comparison")
    parser.add_argument("--allocs-warn-ratio", type=float, default=1.5,
                        help="allocations/op ratio that triggers a warning")
    parser.add_argument("--allocs-fail-ratio", type=float, default=2.0,
                        help="allocations/op ratio that fails the comparison")
    args = parser.parse_args()

    if not os.path.exists(args.baseline):
        msg = (f"No baseline found at `{args.baseline}`; skipping comparison.\n"
               "Seed one with the manual 'Benchmark' workflow "
               "(update_baseline=true).")
        print(msg)
        if args.summary_md:
            with open(args.summary_md, "w", encoding="utf-8") as fh:
                fh.write("## Benchmark comparison\n\n" + msg + "\n")
        return 0

    base = load(args.baseline)
    cur = load(args.current)

    failed: list[str] = []
    warned: list[str] = []
    rows: list[str] = []

    bmap = {b.get("name"): b for b in base.get("benchmarks", [])}

    for c in cur.get("benchmarks", []):
        name = c.get("name", "?")
        b = bmap.get(name)
        if b is None:
            rows.append(f"| `{name}` | – | {fmt_ops(c['ops_per_sec'])} | "
                        f"new | – | n/a |")
            continue

        b_ops = float(b.get("ops_per_sec", 0.0))
        c_ops = float(c.get("ops_per_sec", 0.0))
        status = "ok"

        if b_ops > 0:
            slow_pct = (1.0 - c_ops / b_ops) * 100.0
            if slow_pct >= args.fail_pct:
                status = "**REGRESSION**"
                failed.append(
                    f"{name}: {slow_pct:.0f}% slower than baseline "
                    f"({fmt_ops(c_ops)} vs {fmt_ops(b_ops)} ops/s)")
            elif slow_pct >= args.warn_pct:
                status = "slower"
                warned.append(
                    f"{name}: {slow_pct:.0f}% slower than baseline")

        b_alloc = float(b.get("allocs_per_op", 0.0))
        c_alloc = float(c.get("allocs_per_op", 0.0))
        if b_alloc > 0 and (c_alloc - b_alloc) >= 1.0:
            ratio = c_alloc / b_alloc
            if ratio >= args.allocs_fail_ratio:
                status = "**REGRESSION**"
                failed.append(
                    f"{name}: allocations/op grew from {b_alloc:.2f} to "
                    f"{c_alloc:.2f}")
            elif ratio >= args.allocs_warn_ratio:
                if status == "ok":
                    status = "more allocs"
                warned.append(
                    f"{name}: allocations/op grew from {b_alloc:.2f} to "
                    f"{c_alloc:.2f}")

        alloc_cell = f"{b_alloc:.2f} → {c_alloc:.2f}"
        rows.append(
            f"| `{name}` | {fmt_ops(b_ops)} | {fmt_ops(c_ops)} | "
            f"{pct(c_ops, b_ops)} | {alloc_cell} | {status} |")

    removed = [b.get("name") for b in base.get("benchmarks", [])
               if b.get("name") not in {c.get("name")
                                        for c in cur.get("benchmarks", [])}]

    notes = compare_meta(base, cur)
    if removed:
        notes.append("Baseline contains benchmarks missing from this run: "
                     + ", ".join(f"`{r}`" for r in removed))

    bmeta = base.get("meta", {})
    cmeta = cur.get("meta", {})
    print(f"Baseline: {bmeta.get('timestamp', '?')} "
          f"(commit {str(bmeta.get('commit', '?'))[:10]})")
    print(f"Current:  {cmeta.get('timestamp', '?')} "
          f"(commit {str(cmeta.get('commit', '?'))[:10]})")
    print()
    for row in rows:
        print(row)
    print()
    for n in notes:
        print(f"note: {n}")
    for w in warned:
        print(f"warning: {w}")
    for f in failed:
        print(f"FAILURE: {f}")

    if args.summary_md:
        with open(args.summary_md, "w", encoding="utf-8") as fh:
            fh.write("## Benchmark comparison (current vs baseline)\n\n")
            fh.write(f"- Baseline: `{bmeta.get('timestamp', '?')}` "
                     f"commit `{str(bmeta.get('commit', '?'))[:10]}` "
                     f"on `{bmeta.get('cpu', '?')}`\n")
            fh.write(f"- Current: `{cmeta.get('timestamp', '?')}` "
                     f"commit `{str(cmeta.get('commit', '?'))[:10]}` "
                     f"on `{cmeta.get('cpu', '?')}`\n")
            fh.write(f"- Thresholds: warn ≥ {args.warn_pct:g}% slower, "
                     f"fail ≥ {args.fail_pct:g}% slower, "
                     f"allocs warn ≥ {args.allocs_warn_ratio:g}x, "
                     f"fail ≥ {args.allocs_fail_ratio:g}x\n\n")
            fh.write("| Benchmark | ops/s (base) | ops/s (cur) | Δ speed | "
                     "allocs/op | status |\n")
            fh.write("|---|---:|---:|---:|---:|---|\n")
            for row in rows:
                fh.write(row + "\n")
            if notes or warned or failed:
                fh.write("\n")
                for n in notes:
                    fh.write(f"> note: {n}\n")
                for w in warned:
                    fh.write(f"> warning: {w}\n")
                for f in failed:
                    fh.write(f"> **FAILURE**: {f}\n")

    if failed:
        print(f"\ncomparison FAILED: {len(failed)} regression(s)")
        return 1

    print("\ncomparison OK"
          + (f" ({len(warned)} warning(s))" if warned else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
