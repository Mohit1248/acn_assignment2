import csv
import math

QUANTUM = 8192
N_REQUESTS = 1000
REFERENCE_SERVER_THREADS = 4
REFERENCE_CLIENT_THREADS = 8
SINGLE_SERVER_THREADS = 1

_INT_COLUMNS = ("bytes", "rounds", "forfeited_bytes", "arrival_ns", "start_ns", "finish_ns")

def load_rows(csv_path):
    with open(csv_path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in _INT_COLUMNS:
            r[k] = int(r[k])
    return rows

def exclude_seed_rows(rows, seed_count):
    rows_sorted = sorted(rows, key=lambda r: r["arrival_ns"])
    return rows_sorted[seed_count:]

def nearest_rank_percentile(sorted_values, p):
    n = len(sorted_values)
    if n == 0:
        return None
    idx = -((-int(p) * n) // 100) - 1
    idx = max(0, min(idx, n - 1))
    return sorted_values[idx]

def size_class(filename, num_bytes):
    name = filename.lower()
    if "small" in name:
        return "small"
    if "medium" in name:
        return "medium"
    if "large" in name:
        return "large"
    if num_bytes < 5 * 1024:
        return "small"
    if num_bytes < 80 * 1024:
        return "medium"
    return "large"
