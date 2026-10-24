#!/usr/bin/env python3
"""Compares two experiment summaries for an obvious performance regression.

Run on the same machine and build type; CI machines and wall-clock times are
not comparable. Node counts are a deterministic work metric; p50/p95 carry
measured noise, so the thresholds are deliberately generous.
"""
import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def check(label, before, after, factor, absolute_ms):
    limit = before * factor + absolute_ms
    status = "OK" if after <= limit else "REGRESSION"
    print(f"{label}: {before:.1f} -> {after:.1f} (limit {limit:.1f}) {status}")
    return status == "OK"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--p50-factor", type=float, default=1.5)
    parser.add_argument("--p95-factor", type=float, default=2.0)
    parser.add_argument("--absolute-ms", type=float, default=5.0)
    args = parser.parse_args()

    before = load(args.baseline)
    after = load(args.candidate)
    if before.get("name") != after.get("name"):
        print("experiment names differ; refusing to compare", file=sys.stderr)
        return 2
    if before.get("seed") != after.get("seed"):
        print("seeds differ; refusing to compare", file=sys.stderr)
        return 2

    ok = True
    for key in ("a_stats", "b_stats"):
        b = before[key]
        a = after[key]
        ok &= check(f"{key} p50", b["p50_ms"], a["p50_ms"],
                    args.p50_factor, args.absolute_ms)
        ok &= check(f"{key} p95", b["p95_ms"], a["p95_ms"],
                    args.p95_factor, args.absolute_ms * 2)
        ok &= check(f"{key} nodes", b["nodes"], a["nodes"], 1.10, 0)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
