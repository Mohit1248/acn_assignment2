
import os
import sys

from metrics import CELLS, CLASSES, REFERENCE, fmt_count, load_all

results = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results")
D = load_all(results)

def us(ns):
    return ns / 1000.0

print("== waiting / response p50, p99 (us) and throughput (req/s) ==")
for cell in CELLS:
    m = D["cells"].get(cell)
    if not m:
        continue
    print(f"{cell:9s} wait p50 {us(m['wait_p50_ns']):8.0f} p99 {us(m['wait_p99_ns']):9.0f} | "
          f"resp p50 {us(m['resp_p50_ns']):8.0f} p99 {us(m['resp_p99_ns']):9.0f} | thr {m['throughput']:8.1f}")
    if cell in D["rerun"]:
        r = D["rerun"][cell]
        print(f"  rerun   wait p50 {us(r['wait_p50_ns']):8.0f} p99 {us(r['wait_p99_ns']):9.0f} | "
              f"resp p50 {us(r['resp_p50_ns']):8.0f} p99 {us(r['resp_p99_ns']):9.0f} | thr {r['throughput']:8.1f}")

print("\n== normalised slowdown (ns/byte) by size class, reference config only (A21) ==")
for cell in REFERENCE:
    m = D["cells"].get(cell)
    if not m:
        continue
    parts = "  ".join(f"{c}: med={m['by_class'][c]['slow_med']:.1f} p99={m['by_class'][c]['slow_p99']:.1f}" for c in CLASSES)
    print(f"{cell:9s} {parts}")

print("\n== waiting by size class (avg us) - who each policy penalises ==")
for cell in CELLS:
    m = D["cells"].get(cell)
    if not m:
        continue
    parts = "  ".join(f"{c}: {us(m['by_class'][c]['wait_avg_ns']):7.0f}" for c in CLASSES)
    print(f"{cell:9s} {parts}")

print("\n== rr vs drr (A29): forfeited_bytes by class, rounds/GET, A14 fires ==")
for cell in ("rr_ref", "drr_ref"):
    m = D["cells"].get(cell)
    if not m:
        continue
    parts = "  ".join(f"{c}: forf={m['by_class'][c]['forfeited']:9.0f} rounds/GET={m['by_class'][c]['rounds_mean_get']:.1f}" for c in CLASSES)
    print(f"{cell:8s} A14={fmt_count(D['a14'].get(cell))}  {parts}")
