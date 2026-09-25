#!/usr/bin/env python3
"""C6: waiting p50/p99, throughput, and (optionally) normalised slowdown
per size class, computed from one run's metrics CSV (A19-A22).

Usage:
    python3 analysis.py <csv_path> --seed-count 3 [--slowdown]

--seed-count must equal the number of files in the workload directory used
for that run (each seeded exactly once by `load`, see common.py's
exclude_seed_rows for why sorting by arrival_ns reliably identifies them).
"""
import argparse
import sys

from common import exclude_seed_rows, load_rows, nearest_rank_percentile, size_class


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path")
    ap.add_argument("--seed-count", type=int, required=True,
                     help="number of workload files (seeding PUTs to exclude)")
    ap.add_argument("--slowdown", action="store_true",
                     help="also report per-size-class normalised slowdown (reference config only, A21)")
    args = ap.parse_args()

    rows = exclude_seed_rows(load_rows(args.csv_path), args.seed_count)
    if not rows:
        print("error: no non-seed rows found - check --seed-count", file=sys.stderr)
        sys.exit(1)

    waiting = sorted(r["start_ns"] - r["arrival_ns"] for r in rows)
    n = len(rows)
    p50 = nearest_rank_percentile(waiting, 50)
    p99 = nearest_rank_percentile(waiting, 99)

    max_finish = max(r["finish_ns"] for r in rows)
    min_arrival = min(r["arrival_ns"] for r in rows)
    window_s = (max_finish - min_arrival) / 1e9
    throughput = n / window_s if window_s > 0 else float("inf")

    print(f"N (excl. {args.seed_count} seed rows) = {n}")
    print(f"waiting p50 = {p50} ns")
    print(f"waiting p99 = {p99} ns")
    print(f"throughput  = {throughput:.2f} req/s")

    if args.slowdown:
        by_class = {}
        for r in rows:
            if r["bytes"] == 0:
                continue  # slowdown = response/bytes is undefined for a 0-byte transfer
            sd_ns_per_byte = (r["finish_ns"] - r["arrival_ns"]) / r["bytes"]
            by_class.setdefault(size_class(r["filename"], r["bytes"]), []).append(sd_ns_per_byte)

        print("\nnormalised slowdown (ns/byte) by size class:")
        for cls in ("small", "medium", "large"):
            vals = sorted(by_class.get(cls, []))
            if not vals:
                continue
            med = nearest_rank_percentile(vals, 50)
            p99v = nearest_rank_percentile(vals, 99)
            print(f"  {cls}: median={med:.3f}  p99={p99v:.3f}  (n={len(vals)})")


if __name__ == "__main__":
    main()
