#!/usr/bin/env python3
"""C7: rr vs drr comparison (A29) - forfeited_bytes per size class, how
often the A14 escape hatch fired (rr only, parsed from that run's server
stderr log), and the normalised slowdown of the long-line file under each
policy.

Usage:
    python3 compare_rr_drr.py --rr-csv results/rr_ref.csv \\
        --drr-csv results/drr_ref.csv --rr-log results/rr_ref.server.log \\
        --seed-count 3
"""
import argparse
import re
import sys

from common import exclude_seed_rows, load_rows, nearest_rank_percentile, size_class

A14_RE = re.compile(r"^A14 request_id=(\d+) filename=(\S+) line_bytes=(\d+) quantum=(\d+)$")


def count_a14_fires(log_path):
    count = 0
    with open(log_path) as f:
        for line in f:
            if A14_RE.match(line.strip()):
                count += 1
    return count


def forfeited_by_class(rows):
    totals = {}
    for r in rows:
        cls = size_class(r["filename"], r["bytes"])
        totals[cls] = totals.get(cls, 0) + r["forfeited_bytes"]
    return totals


def slowdown_for_file(rows, filename):
    return sorted(
        (r["finish_ns"] - r["arrival_ns"]) / r["bytes"]
        for r in rows
        if r["filename"] == filename and r["bytes"] > 0
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rr-csv", required=True)
    ap.add_argument("--drr-csv", required=True)
    ap.add_argument("--rr-log", required=True, help="rr run's server stderr log (for the A14 fire count)")
    ap.add_argument("--seed-count", type=int, required=True)
    ap.add_argument("--long-line-file", default="large.txt")
    args = ap.parse_args()

    rr_rows = exclude_seed_rows(load_rows(args.rr_csv), args.seed_count)
    drr_rows = exclude_seed_rows(load_rows(args.drr_csv), args.seed_count)

    print("forfeited_bytes by size class:")
    for cls in ("small", "medium", "large"):
        rr_f = forfeited_by_class(rr_rows).get(cls, 0)
        drr_f = forfeited_by_class(drr_rows).get(cls, 0)
        print(f"  {cls}: rr={rr_f}  drr={drr_f}")

    fires = count_a14_fires(args.rr_log)
    print(f"\nA14 fired {fires} time(s) under rr (drr never takes this path, A16)")

    for label, rows in (("rr", rr_rows), ("drr", drr_rows)):
        vals = slowdown_for_file(rows, args.long_line_file)
        if not vals:
            print(f"\n{label}: no rows for {args.long_line_file}", file=sys.stderr)
            continue
        med = nearest_rank_percentile(vals, 50)
        p99 = nearest_rank_percentile(vals, 99)
        print(f"\n{label} slowdown for {args.long_line_file} (ns/byte): "
              f"median={med:.3f}  p99={p99:.3f}  (n={len(vals)})")


if __name__ == "__main__":
    main()
