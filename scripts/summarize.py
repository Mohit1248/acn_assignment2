#!/usr/bin/env python3
"""Prints every number the report uses: median [min-max] over the repeated runs.

    python3 scripts/summarize.py [results_dir]
"""
import os
import sys

from metrics import ALL_CELLS, CLASSES, REFERENCE, across, across_a14, fmt_count, load_all

results = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results")
d = load_all(results)
K = len(d["runs"])
print(f"{K} repeated runs: {[r['name'] for r in d['runs']]}\n")


def fmt(a, scale=1.0, nd=0):
    return f"{a['median'] / scale:.{nd}f} [{a['min'] / scale:.{nd}f}-{a['max'] / scale:.{nd}f}]"


print("== waiting p50 / p99 (us) and throughput (req/s): median [min-max] ==")
for cell in ALL_CELLS:
    if not any(cell in r["cells"] for r in d["runs"]):
        continue
    p50 = across(d, cell, lambda m: m["wait_p50_ns"])
    p99 = across(d, cell, lambda m: m["wait_p99_ns"])
    thr = across(d, cell, lambda m: m["throughput"])
    r50 = across(d, cell, lambda m: m["resp_p50_ns"])
    r99 = across(d, cell, lambda m: m["resp_p99_ns"])
    print(f"{cell:9s} wait p50 {fmt(p50, 1000):20s} p99 {fmt(p99, 1000):24s} | resp p50 {fmt(r50, 1000):20s} p99 {fmt(r99, 1000):24s} | thr {fmt(thr):18s}")

print("\n== normalised slowdown median (ns/byte): median [min-max]; and p99 ==")
for cell in ALL_CELLS:
    if not any(cell in r["cells"] for r in d["runs"]):
        continue
    parts = []
    for c in CLASSES:
        med = across(d, cell, lambda m, c=c: m["by_class"][c]["slow_med"])
        p99 = across(d, cell, lambda m, c=c: m["by_class"][c]["slow_p99"])
        parts.append(f"{c}: med {fmt(med, 1, 0)} p99 {p99['median']:.0f}")
    print(f"{cell:9s} " + " | ".join(parts))

print("\n== waiting by size class (us, median of run p50 / p99) ==")
for cell in REFERENCE + ("fcfs_st1", "rr_st1"):
    parts = []
    for c in CLASSES:
        w50 = across(d, cell, lambda m, c=c: m["by_class"][c]["wait_p50_ns"])["median"] / 1000
        w99 = across(d, cell, lambda m, c=c: m["by_class"][c]["wait_p99_ns"])["median"] / 1000
        parts.append(f"{c}: {w50:6.0f}/{w99:6.0f}")
    print(f"{cell:9s} " + "  ".join(parts))

print("\n== rr vs drr: forfeited_bytes by class, rounds per GET, A14 fires (median over runs) ==")
for cell in ("rr_ref", "drr_ref", "rr_st1", "drr_st1"):
    if not any(cell in r["cells"] for r in d["runs"]):
        continue
    parts = []
    for c in CLASSES:
        f = across(d, cell, lambda m, c=c: m["by_class"][c]["forfeited"])["median"]
        r = across(d, cell, lambda m, c=c: m["by_class"][c]["rounds_mean_get"])["median"]
        parts.append(f"{c}: forf={f:9.0f} rounds/GET={r:4.1f}")
    print(f"{cell:8s} A14={fmt_count(across_a14(d, cell)['median'])}  " + "  ".join(parts))

print("\n== A30 aside: --p ==")
for name, m in d["aside"].items():
    print(f"{name}: send_calls={m['send_calls']} throughput={m['throughput']:.0f} wait_p50={m['wait_p50_ns'] / 1000:.0f}us")
